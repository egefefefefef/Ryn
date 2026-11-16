#include "sema_internal.h"

static Type *prim(Sema *sema, TypeKind kind) {
    return type_prim(&sema->types, kind);
}

static void type_name_string(Type *type, char *buf, size_t len) {
    type_to_string_buf(type, buf, len);
}

/* ------------------------------------------------------------------ */
/* Variable declarations                                              */
/* ------------------------------------------------------------------ */

static void check_var_decl(Sema *sema, AstVarDecl *decl) {
    Type *declared = NULL;
    if (decl->type_syntax) {
        declared = sema_resolve_type(sema, decl->type_syntax);
        if (declared->kind == TY_VOID) {
            sema_error(sema, decl->base.pos, (int)strlen(decl->name), E_TYPE_MISMATCH,
                       "variable '%s' cannot have type void", decl->name);
            declared = prim(sema, TY_ERROR);
        }
    }
    bool has_init = decl->init != NULL;
    Type *inferred = NULL;
    if (has_init) {
        ExprValue value;
        sema_check_expr(sema, decl->init, declared, &value);
        if (declared) {
            sema_coerce(sema, decl->init, &value, declared, "variable initializer");
        } else {
            if (type_is_untyped(value.type)) {
                sema_default_expr_type(sema, decl->init, &value);
            }
            if (value.type->kind == TY_VOID) {
                sema_error(sema, decl->init->pos, 1, E_VOID_VALUE,
                           "cannot initialize '%s' with a void expression", decl->name);
                inferred = prim(sema, TY_ERROR);
            } else if (value.type->kind == TY_NULL) {
                sema_error_note(sema, decl->init->pos, 1, E_TYPE_MISMATCH,
                                "add a type annotation such as 'ptr<i32>'",
                                "cannot infer the type of '%s' from null", decl->name);
                inferred = prim(sema, TY_ERROR);
            } else {
                inferred = value.type;
            }
        }
    }
    if (!declared) {
        declared = inferred ? inferred : prim(sema, TY_ERROR);
    }
    if (!has_init && !decl->is_mut) {
        sema_error_note(sema, decl->base.pos, (int)strlen(decl->name), E_UNINITIALIZED,
                        "use 'var' for a variable that is assigned later",
                        "immutable variable '%s' must be initialized", decl->name);
    }
    SemaSymbol *symbol =
        sema_scope_define(sema, decl->name, declared, decl->is_mut, decl->base.pos, false);
    symbol->initialized = has_init;
    decl->symbol = symbol;
}

/* ------------------------------------------------------------------ */
/* Assignment                                                         */
/* ------------------------------------------------------------------ */

static void describe_target(AstExpr *target, char *buf, size_t len) {
    if (target->kind == AST_IDENT) {
        snprintf(buf, len, "'%s'", ((AstIdent *)target)->name);
    } else if (target->kind == AST_FIELD) {
        snprintf(buf, len, "field '%s'", ((AstField *)target)->name);
    } else if (target->kind == AST_INDEX) {
        snprintf(buf, len, "array element");
    } else if (target->kind == AST_UNARY) {
        snprintf(buf, len, "dereferenced pointer");
    } else {
        snprintf(buf, len, "expression");
    }
}

static void report_immutable(Sema *sema, AstExpr *target) {
    char name[96];
    describe_target(target, name, sizeof(name));
    const char *note = "declare it with 'var' instead of 'let'";
    if (target->kind == AST_IDENT) {
        SemaSymbol *symbol = ((AstIdent *)target)->symbol;
        if (symbol && symbol->is_param) {
            note = "parameters are immutable; copy the value into a 'var' if needed";
        } else if (symbol && symbol->is_loop_var) {
            note = "the loop variable is immutable inside the loop body";
        }
    }
    sema_error_note(sema, target->pos, 1, E_IMMUTABLE_ASSIGN, note,
                    "cannot assign to %s because it is immutable", name);
}

static void check_assign(Sema *sema, AstAssign *assign) {
    ExprValue target;
    bool saved_mode = sema->lvalue_mode;
    sema->lvalue_mode = assign->op == BIN_NONE;
    sema_check_expr(sema, assign->target, NULL, &target);
    sema->lvalue_mode = saved_mode;

    if (!type_is_error(target.type)) {
        if (!target.is_lvalue) {
            char name[96];
            describe_target(assign->target, name, sizeof(name));
            sema_error_note(sema, assign->target->pos, 1, E_ASSIGN_TARGET,
                            "assignments can target variables, fields, array elements and "
                            "dereferenced pointers",
                            "cannot assign to %s", name);
        } else if (!target.is_mutable) {
            report_immutable(sema, assign->target);
        }
    }

    if (assign->op == BIN_NONE) {
        ExprValue value;
        sema_check_expr(sema, assign->value, target.type, &value);
        sema_coerce(sema, assign->value, &value, target.type, "assignment");
    } else {
        Type *type = target.type;
        bool valid = true;
        if (type_is_error(type)) {
            valid = false;
        } else if (assign->op == BIN_MOD) {
            if (!type_is_integer(type)) {
                char type_buf[64];
                type_name_string(type, type_buf, sizeof(type_buf));
                sema_error(sema, assign->base.pos, 1, E_BAD_OPERATOR,
                           "'%%=' requires an integer target, found '%s'", type_buf);
                valid = false;
            }
        } else if (!type_is_numeric(type)) {
            char type_buf[64];
            type_name_string(type, type_buf, sizeof(type_buf));
            sema_error_note(sema, assign->base.pos, 1, E_BAD_OPERATOR,
                            "use a normal assignment for non numeric values",
                            "compound assignment '%s=' requires a number, found '%s'",
                            binop_name(assign->op), type_buf);
            valid = false;
        }
        ExprValue value;
        sema_check_expr(sema, assign->value, valid ? type : NULL, &value);
        if (valid) {
            sema_coerce(sema, assign->value, &value, type, "compound assignment");
        }
    }
    sema_mark_initialized(assign->target);
}

/* ------------------------------------------------------------------ */
/* Control flow statements                                            */
/* ------------------------------------------------------------------ */

static void check_if(Sema *sema, AstIf *if_stmt) {
    sema_check_condition(sema, if_stmt->cond);

    FlowState base;
    sema_flow_capture(sema, &base);

    sema_check_block(sema, if_stmt->then_block);
    FlowState then_state;
    sema_flow_capture(sema, &then_state);

    if (if_stmt->else_branch) {
        FlowState else_state;
        sema_check_stmt(sema, if_stmt->else_branch);
        sema_flow_capture(sema, &else_state);
        sema_flow_merge_and(&then_state, &else_state);
        sema_flow_restore(&then_state);
        sema_flow_free(&else_state);
    } else {
        sema_flow_merge_and(&base, &then_state);
        sema_flow_restore(&base);
    }
    sema_flow_free(&base);
    sema_flow_free(&then_state);
}

static void check_while(Sema *sema, AstWhile *while_stmt) {
    sema_check_condition(sema, while_stmt->cond);
    FlowState base;
    sema_flow_capture(sema, &base);
    sema->loop_depth++;
    sema_check_block(sema, while_stmt->body);
    sema->loop_depth--;
    /* The body may run zero times: only the state before the loop is certain. */
    sema_flow_restore(&base);
    sema_flow_free(&base);
}

static void check_for(Sema *sema, AstFor *for_stmt) {
    ExprValue start;
    ExprValue end;
    sema_check_expr(sema, for_stmt->start, NULL, &start);
    sema_check_expr(sema, for_stmt->end, NULL, &end);

    if (type_is_untyped(start.type) && type_is_untyped(end.type)) {
        Type *chosen = start.type->kind == TY_UNTYPED_FLOAT || end.type->kind == TY_UNTYPED_FLOAT
                           ? prim(sema, TY_F64)
                           : prim(sema, TY_I32);
        sema_default_expr_type(sema, for_stmt->start, &start);
        sema_default_expr_type(sema, for_stmt->end, &end);
        if (!type_is_error(start.type)) {
            start.type = chosen;
        }
        if (!type_is_error(end.type)) {
            end.type = chosen;
        }
    } else if (type_is_untyped(start.type)) {
        Type *chosen = end.type;
        sema_coerce(sema, for_stmt->start, &start, chosen, "range bound");
    } else if (type_is_untyped(end.type)) {
        Type *chosen = start.type;
        sema_coerce(sema, for_stmt->end, &end, chosen, "range bound");
    } else if (!type_is_error(start.type) && !type_eq(start.type, end.type)) {
        char start_buf[64];
        char end_buf[64];
        type_name_string(start.type, start_buf, sizeof(start_buf));
        type_name_string(end.type, end_buf, sizeof(end_buf));
        sema_error_note(sema, for_stmt->base.pos, 1, E_TYPE_MISMATCH,
                        "both bounds must have the same integer type",
                        "range bounds have different types: '%s' and '%s'", start_buf, end_buf);
    }
    Type *loop_type = start.type;
    if (type_is_error(loop_type)) {
        loop_type = prim(sema, TY_I32);
    } else if (!type_is_integer(loop_type)) {
        char type_buf[64];
        type_name_string(loop_type, type_buf, sizeof(type_buf));
        sema_error_note(sema, for_stmt->base.pos, 1, E_TYPE_MISMATCH,
                        "ranges iterate over integers only",
                        "for loop range must be an integer, found '%s'", type_buf);
        loop_type = prim(sema, TY_I32);
    }

    sema_scope_push(sema, false);
    SemaSymbol *symbol =
        sema_scope_define(sema, for_stmt->var, loop_type, false, for_stmt->var_pos, false);
    symbol->initialized = true;
    symbol->is_loop_var = true;
    for_stmt->symbol = symbol;

    FlowState base;
    sema_flow_capture(sema, &base);
    sema->loop_depth++;
    sema_check_block(sema, for_stmt->body);
    sema->loop_depth--;
    sema_flow_restore(&base);
    sema_flow_free(&base);
    sema_scope_pop(sema);
}

static void check_return(Sema *sema, AstReturn *ret) {
    if (!sema->current_function) {
        sema_error(sema, ret->base.pos, 5, E_RETURN_OUTSIDE_FN,
                   "'return' is only allowed inside a function");
        if (ret->value) {
            ExprValue value;
            sema_check_expr(sema, ret->value, NULL, &value);
        }
        return;
    }
    Type *return_type = sema->current_function->return_type;
    if (ret->value) {
        ExprValue value;
        sema_check_expr(sema, ret->value,
                        return_type->kind == TY_VOID ? NULL : return_type, &value);
        if (return_type->kind == TY_VOID) {
            sema_error_note(sema, ret->value->pos, 1, E_TYPE_MISMATCH,
                            "write 'return' without a value",
                            "function '%s' does not return a value",
                            sema->current_function->name);
        } else {
            sema_coerce(sema, ret->value, &value, return_type, "return statement");
        }
    } else if (return_type->kind != TY_VOID && !type_is_error(return_type)) {
        char type_buf[64];
        type_name_string(return_type, type_buf, sizeof(type_buf));
        sema_error_note(sema, ret->base.pos, 6, E_MISSING_RETURN,
                        "return a value of the declared type",
                        "function '%s' must return a value of type '%s'",
                        sema->current_function->name, type_buf);
    }
}

/* ------------------------------------------------------------------ */
/* Blocks and statements                                              */
/* ------------------------------------------------------------------ */

void sema_check_block(Sema *sema, AstBlock *block) {
    if (!block) {
        return;
    }
    sema_scope_push(sema, false);
    bool unreachable = false;
    for (size_t i = 0; i < block->count; i++) {
        AstStmt *stmt = block->stmts[i];
        if (unreachable) {
            sema_warn(sema, stmt->pos, 1, W_UNREACHABLE,
                      "this statement is unreachable");
        }
        sema_check_stmt(sema, stmt);
        if (sema_stmt_exits(stmt)) {
            unreachable = true;
        }
    }
    sema_scope_pop(sema);
}

void sema_check_stmt(Sema *sema, AstStmt *stmt) {
    if (!stmt) {
        return;
    }
    switch (stmt->kind) {
    case AST_VAR_DECL:
        check_var_decl(sema, (AstVarDecl *)stmt);
        break;
    case AST_ASSIGN:
        check_assign(sema, (AstAssign *)stmt);
        break;
    case AST_EXPR_STMT: {
        AstExprStmt *expr_stmt = (AstExprStmt *)stmt;
        ExprValue value;
        sema_check_expr(sema, expr_stmt->expr, NULL, &value);
        if (type_is_untyped(value.type)) {
            sema_default_expr_type(sema, expr_stmt->expr, &value);
        }
        if (value.type->kind != TY_VOID) {
            if (expr_stmt->expr->kind == AST_BINARY ||
                (expr_stmt->expr->kind == AST_UNARY &&
                 ((AstUnary *)expr_stmt->expr)->op != UN_DEREF)) {
                sema_warn(sema, stmt->pos, 1, W_UNUSED_RESULT,
                          "expression result is unused; remove it or use a call");
            }
        }
        break;
    }
    case AST_IF:
        check_if(sema, (AstIf *)stmt);
        break;
    case AST_WHILE:
        check_while(sema, (AstWhile *)stmt);
        break;
    case AST_FOR:
        check_for(sema, (AstFor *)stmt);
        break;
    case AST_RETURN:
        check_return(sema, (AstReturn *)stmt);
        break;
    case AST_BREAK:
        if (sema->loop_depth == 0) {
            sema_error_note(sema, stmt->pos, 5, E_BREAK_OUTSIDE_LOOP,
                            "break can only leave a loop", "'break' outside of a loop");
        }
        break;
    case AST_CONTINUE:
        if (sema->loop_depth == 0) {
            sema_error_note(sema, stmt->pos, 8, E_BREAK_OUTSIDE_LOOP,
                            "continue can only be used inside a loop",
                            "'continue' outside of a loop");
        }
        break;
    case AST_BLOCK_STMT:
        sema_check_block(sema, ((AstBlockStmt *)stmt)->block);
        break;
    default:
        sema_error(sema, stmt->pos, 1, E_SYNTAX, "unsupported statement");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Function bodies                                                    */
/* ------------------------------------------------------------------ */

void sema_check_function(Sema *sema, AstFnDecl *decl) {
    FunctionInfo *fn = decl->info;
    sema->current_function = fn;
    sema->loop_depth = 0;
    sema->locals_head = NULL;

    sema_scope_push(sema, true);
    for (size_t i = 0; i < decl->nparams; i++) {
        AstParam *param = &decl->params[i];
        SemaSymbol *symbol =
            sema_scope_define(sema, param->name, param->type, false, param->pos, true);
        symbol->initialized = true;
        symbol->read = true;
        param->symbol = symbol;
    }

    sema->function_depth++;
    sema_check_block(sema, decl->body);
    sema->function_depth--;

    if (!type_is_error(fn->return_type) && fn->return_type->kind != TY_VOID &&
        !sema_block_always_returns(decl->body)) {
        char type_buf[64];
        type_name_string(fn->return_type, type_buf, sizeof(type_buf));
        sema_error_note(sema, decl->base.pos, (int)strlen(fn->name), E_MISSING_RETURN,
                        "add a 'return' or cover all paths with if/else",
                        "function '%s' must return a value of type '%s'", fn->name, type_buf);
    }

    for (SemaSymbol *symbol = sema->locals_head; symbol; symbol = symbol->next_local) {
        if (symbol->is_param || symbol->is_loop_var || symbol->read) {
            continue;
        }
        if (symbol->name[0] == '_') {
            continue;
        }
        sema_warn_note(sema, symbol->pos, (int)strlen(symbol->name), W_UNUSED_VARIABLE,
                       "prefix the name with '_' to silence this warning",
                       "variable '%s' is never used", symbol->name);
    }

    sema_scope_pop(sema);
    sema->current_function = NULL;
    sema->locals_head = NULL;
}
