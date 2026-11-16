#include "ast.h"

#include <inttypes.h>

const char *unop_name(UnOp op) {
    switch (op) {
    case UN_NEG: return "-";
    case UN_NOT: return "not";
    case UN_BNOT: return "~";
    case UN_ADDR: return "&";
    case UN_DEREF: return "*";
    default: return "?";
    }
}

const char *binop_name(BinOp op) {
    switch (op) {
    case BIN_ADD: return "+";
    case BIN_SUB: return "-";
    case BIN_MUL: return "*";
    case BIN_DIV: return "/";
    case BIN_MOD: return "%";
    case BIN_AND: return "&";
    case BIN_OR: return "|";
    case BIN_XOR: return "^";
    case BIN_SHL: return "<<";
    case BIN_SHR: return ">>";
    case BIN_EQ: return "==";
    case BIN_NE: return "!=";
    case BIN_LT: return "<";
    case BIN_LE: return "<=";
    case BIN_GT: return ">";
    case BIN_GE: return ">=";
    case BIN_LAND: return "and";
    case BIN_LOR: return "or";
    default: return "?";
    }
}

bool binop_is_comparison(BinOp op) {
    return op >= BIN_EQ && op <= BIN_GE;
}

bool binop_is_logical(BinOp op) {
    return op == BIN_LAND || op == BIN_LOR;
}

bool binop_is_arithmetic(BinOp op) {
    switch (op) {
    case BIN_ADD:
    case BIN_SUB:
    case BIN_MUL:
    case BIN_DIV:
    case BIN_MOD:
        return true;
    default:
        return false;
    }
}

static void *ast_alloc(Arena *arena, size_t size) {
    return arena_calloc(arena, 1, size);
}

AstProgram *ast_program_new(Arena *arena) {
    AstProgram *program = ast_alloc(arena, sizeof(AstProgram));
    program->arena = arena;
    AstDeclVec_init(&program->decls);
    return program;
}

void ast_program_add(AstProgram *program, AstDecl *decl) {
    AstDeclVec_push(&program->decls, decl);
}

void ast_program_free(AstProgram *program) {
    if (!program) {
        return;
    }
    /* The AstProgram itself is arena allocated; only its growable declaration
     * vector needs to be released. */
    AstDeclVec_free(&program->decls);
}

/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */

AstType *ast_type_name(Arena *arena, SourcePos pos, const char *name) {
    AstType *type = ast_alloc(arena, sizeof(AstType));
    type->kind = AST_TYPE_NAME;
    type->pos = pos;
    type->name = arena_strdup(arena, name);
    return type;
}

AstType *ast_type_ptr(Arena *arena, SourcePos pos, AstType *elem) {
    AstType *type = ast_alloc(arena, sizeof(AstType));
    type->kind = AST_TYPE_PTR;
    type->pos = pos;
    type->elem = elem;
    return type;
}

AstType *ast_type_array(Arena *arena, SourcePos pos, AstType *elem, int64_t length) {
    AstType *type = ast_alloc(arena, sizeof(AstType));
    type->kind = AST_TYPE_ARRAY;
    type->pos = pos;
    type->elem = elem;
    type->array_length = length;
    return type;
}

AstType *ast_type_fn(Arena *arena, SourcePos pos, AstType **params, size_t nparams, AstType *ret) {
    AstType *type = ast_alloc(arena, sizeof(AstType));
    type->kind = AST_TYPE_FN;
    type->pos = pos;
    type->params = params;
    type->nparams = nparams;
    type->ret = ret;
    return type;
}

AstType *ast_type_primitive(Arena *arena, SourcePos pos, const char *name) {
    return ast_type_name(arena, pos, name);
}

/* ------------------------------------------------------------------ */
/* Expressions                                                        */
/* ------------------------------------------------------------------ */

static AstExpr *expr_base(Arena *arena, NodeKind kind, SourcePos pos) {
    AstExpr *expr = ast_alloc(arena, sizeof(AstExpr));
    expr->kind = kind;
    expr->pos = pos;
    return expr;
}

AstExpr *ast_int_literal(Arena *arena, SourcePos pos, int64_t value, uint64_t uvalue) {
    AstIntLiteral *expr = ast_alloc(arena, sizeof(AstIntLiteral));
    expr->base = *expr_base(arena, AST_INT_LITERAL, pos);
    expr->value = value;
    expr->uvalue = uvalue;
    return (AstExpr *)expr;
}

AstExpr *ast_float_literal(Arena *arena, SourcePos pos, double value) {
    AstFloatLiteral *expr = ast_alloc(arena, sizeof(AstFloatLiteral));
    expr->base = *expr_base(arena, AST_FLOAT_LITERAL, pos);
    expr->value = value;
    return (AstExpr *)expr;
}

AstExpr *ast_string_literal(Arena *arena, SourcePos pos, const char *value, size_t length) {
    AstStringLiteral *expr = ast_alloc(arena, sizeof(AstStringLiteral));
    expr->base = *expr_base(arena, AST_STRING_LITERAL, pos);
    expr->value = arena_strdup(arena, value ? value : "");
    expr->length = length;
    return (AstExpr *)expr;
}

AstExpr *ast_char_literal(Arena *arena, SourcePos pos, int64_t value) {
    AstCharLiteral *expr = ast_alloc(arena, sizeof(AstCharLiteral));
    expr->base = *expr_base(arena, AST_CHAR_LITERAL, pos);
    expr->value = value;
    return (AstExpr *)expr;
}

AstExpr *ast_bool_literal(Arena *arena, SourcePos pos, bool value) {
    AstBoolLiteral *expr = ast_alloc(arena, sizeof(AstBoolLiteral));
    expr->base = *expr_base(arena, AST_BOOL_LITERAL, pos);
    expr->value = value;
    return (AstExpr *)expr;
}

AstExpr *ast_null_literal(Arena *arena, SourcePos pos) {
    AstExpr *expr = expr_base(arena, AST_NULL_LITERAL, pos);
    return expr;
}

AstExpr *ast_ident(Arena *arena, SourcePos pos, const char *name) {
    AstIdent *expr = ast_alloc(arena, sizeof(AstIdent));
    expr->base = *expr_base(arena, AST_IDENT, pos);
    expr->name = arena_strdup(arena, name);
    return (AstExpr *)expr;
}

AstExpr *ast_unary(Arena *arena, SourcePos pos, UnOp op, AstExpr *operand) {
    AstUnary *expr = ast_alloc(arena, sizeof(AstUnary));
    expr->base = *expr_base(arena, AST_UNARY, pos);
    expr->op = op;
    expr->operand = operand;
    return (AstExpr *)expr;
}

AstExpr *ast_binary(Arena *arena, SourcePos pos, BinOp op, AstExpr *lhs, AstExpr *rhs) {
    AstBinary *expr = ast_alloc(arena, sizeof(AstBinary));
    expr->base = *expr_base(arena, AST_BINARY, pos);
    expr->op = op;
    expr->lhs = lhs;
    expr->rhs = rhs;
    return (AstExpr *)expr;
}

AstExpr *ast_call(Arena *arena, SourcePos pos, AstExpr *callee, AstExpr **args, size_t argc) {
    AstCall *expr = ast_alloc(arena, sizeof(AstCall));
    expr->base = *expr_base(arena, AST_CALL, pos);
    expr->callee = callee;
    expr->args = args;
    expr->argc = argc;
    return (AstExpr *)expr;
}

AstExpr *ast_field(Arena *arena, SourcePos pos, AstExpr *object, const char *name) {
    AstField *expr = ast_alloc(arena, sizeof(AstField));
    expr->base = *expr_base(arena, AST_FIELD, pos);
    expr->object = object;
    expr->name = arena_strdup(arena, name);
    expr->field_index = -1;
    return (AstExpr *)expr;
}

AstExpr *ast_index(Arena *arena, SourcePos pos, AstExpr *object, AstExpr *index) {
    AstIndex *expr = ast_alloc(arena, sizeof(AstIndex));
    expr->base = *expr_base(arena, AST_INDEX, pos);
    expr->object = object;
    expr->index = index;
    return (AstExpr *)expr;
}

AstExpr *ast_cast(Arena *arena, SourcePos pos, AstExpr *operand, AstType *target) {
    AstCast *expr = ast_alloc(arena, sizeof(AstCast));
    expr->base = *expr_base(arena, AST_CAST, pos);
    expr->operand = operand;
    expr->target_type = target;
    return (AstExpr *)expr;
}

AstExpr *ast_array_literal(Arena *arena, SourcePos pos, AstExpr **elements, size_t count,
                           bool is_repeat, int64_t repeat_count) {
    AstArrayLiteral *expr = ast_alloc(arena, sizeof(AstArrayLiteral));
    expr->base = *expr_base(arena, AST_ARRAY_LITERAL, pos);
    expr->elements = elements;
    expr->count = count;
    expr->is_repeat = is_repeat;
    expr->repeat_count = repeat_count;
    return (AstExpr *)expr;
}

AstExpr *ast_error_expr(Arena *arena, SourcePos pos) {
    return expr_base(arena, AST_ERROR_EXPR, pos);
}

AstExpr *ast_struct_literal(Arena *arena, SourcePos pos, AstType *type_syntax, const char **names,
                            AstExpr **values, size_t count) {
    AstStructLiteral *expr = ast_alloc(arena, sizeof(AstStructLiteral));
    expr->base = *expr_base(arena, AST_STRUCT_LITERAL, pos);
    expr->type_syntax = type_syntax;
    expr->field_names = names;
    expr->field_values = values;
    expr->count = count;
    return (AstExpr *)expr;
}

/* ------------------------------------------------------------------ */
/* Statements                                                         */
/* ------------------------------------------------------------------ */

AstBlock *ast_block_new(Arena *arena, SourcePos pos) {
    AstBlock *block = ast_alloc(arena, sizeof(AstBlock));
    block->pos = pos;
    block->arena = arena;
    block->stmts = NULL;
    block->count = 0;
    block->capacity = 0;
    return block;
}

void ast_block_add(AstBlock *block, AstStmt *stmt) {
    if (block->count == block->capacity) {
        size_t new_capacity = block->capacity ? block->capacity * 2 : 4;
        AstStmt **grown = ARENA_ARRAY(block->arena, AstStmt *, new_capacity);
        if (block->stmts) {
            memcpy(grown, block->stmts, block->count * sizeof(AstStmt *));
        }
        block->stmts = grown;
        block->capacity = new_capacity;
    }
    block->stmts[block->count++] = stmt;
}

static AstStmt *stmt_base(Arena *arena, NodeKind kind, SourcePos pos, size_t size) {
    AstStmt *stmt = ast_alloc(arena, size);
    stmt->kind = kind;
    stmt->pos = pos;
    return stmt;
}

AstStmt *ast_var_decl(Arena *arena, SourcePos pos, bool is_mut, const char *name,
                      AstType *type_syntax, AstExpr *init) {
    AstVarDecl *stmt = (AstVarDecl *)stmt_base(arena, AST_VAR_DECL, pos, sizeof(AstVarDecl));
    stmt->is_mut = is_mut;
    stmt->name = arena_strdup(arena, name);
    stmt->type_syntax = type_syntax;
    stmt->init = init;
    return (AstStmt *)stmt;
}

AstStmt *ast_assign(Arena *arena, SourcePos pos, AstExpr *target, AstExpr *value, BinOp op) {
    AstAssign *stmt = (AstAssign *)stmt_base(arena, AST_ASSIGN, pos, sizeof(AstAssign));
    stmt->target = target;
    stmt->value = value;
    stmt->op = op;
    return (AstStmt *)stmt;
}

AstStmt *ast_expr_stmt(Arena *arena, SourcePos pos, AstExpr *expr) {
    AstExprStmt *stmt = (AstExprStmt *)stmt_base(arena, AST_EXPR_STMT, pos, sizeof(AstExprStmt));
    stmt->expr = expr;
    return (AstStmt *)stmt;
}

AstStmt *ast_if(Arena *arena, SourcePos pos, AstExpr *cond, AstBlock *then_block,
                AstStmt *else_branch) {
    AstIf *stmt = (AstIf *)stmt_base(arena, AST_IF, pos, sizeof(AstIf));
    stmt->cond = cond;
    stmt->then_block = then_block;
    stmt->else_branch = else_branch;
    return (AstStmt *)stmt;
}

AstStmt *ast_while(Arena *arena, SourcePos pos, AstExpr *cond, AstBlock *body) {
    AstWhile *stmt = (AstWhile *)stmt_base(arena, AST_WHILE, pos, sizeof(AstWhile));
    stmt->cond = cond;
    stmt->body = body;
    return (AstStmt *)stmt;
}

AstStmt *ast_for(Arena *arena, SourcePos pos, const char *var, SourcePos var_pos, AstExpr *start,
                 AstExpr *end, bool inclusive, AstBlock *body) {
    AstFor *stmt = (AstFor *)stmt_base(arena, AST_FOR, pos, sizeof(AstFor));
    stmt->var = arena_strdup(arena, var);
    stmt->var_pos = var_pos;
    stmt->start = start;
    stmt->end = end;
    stmt->inclusive = inclusive;
    stmt->body = body;
    return (AstStmt *)stmt;
}

AstStmt *ast_return(Arena *arena, SourcePos pos, AstExpr *value) {
    AstReturn *stmt = (AstReturn *)stmt_base(arena, AST_RETURN, pos, sizeof(AstReturn));
    stmt->value = value;
    return (AstStmt *)stmt;
}

AstStmt *ast_break(Arena *arena, SourcePos pos) {
    return stmt_base(arena, AST_BREAK, pos, sizeof(AstStmt));
}

AstStmt *ast_continue(Arena *arena, SourcePos pos) {
    return stmt_base(arena, AST_CONTINUE, pos, sizeof(AstStmt));
}

AstStmt *ast_block_stmt(Arena *arena, SourcePos pos, AstBlock *block) {
    AstBlockStmt *stmt = (AstBlockStmt *)stmt_base(arena, AST_BLOCK_STMT, pos, sizeof(AstBlockStmt));
    stmt->block = block;
    return (AstStmt *)stmt;
}

/* ------------------------------------------------------------------ */
/* Declarations                                                       */
/* ------------------------------------------------------------------ */

AstDecl *ast_fn_decl(Arena *arena, SourcePos pos, const char *name, AstParam *params,
                     size_t nparams, AstType *return_type, AstBlock *body, bool is_extern) {
    AstFnDecl *decl = ast_alloc(arena, sizeof(AstFnDecl));
    decl->base.kind = AST_FN_DECL;
    decl->base.pos = pos;
    decl->name = arena_strdup(arena, name);
    decl->params = params;
    decl->nparams = nparams;
    decl->return_type_syntax = return_type;
    decl->body = body;
    decl->is_extern = is_extern;
    return (AstDecl *)decl;
}

AstDecl *ast_struct_decl(Arena *arena, SourcePos pos, const char *name, AstFieldDecl *fields,
                         size_t nfields) {
    AstStructDecl *decl = ast_alloc(arena, sizeof(AstStructDecl));
    decl->base.kind = AST_STRUCT_DECL;
    decl->base.pos = pos;
    decl->name = arena_strdup(arena, name);
    decl->fields = fields;
    decl->nfields = nfields;
    return (AstDecl *)decl;
}

AstDecl *ast_enum_decl(Arena *arena, SourcePos pos, const char *name, AstEnumVariant *variants,
                       size_t nvariants) {
    AstEnumDecl *decl = ast_alloc(arena, sizeof(AstEnumDecl));
    decl->base.kind = AST_ENUM_DECL;
    decl->base.pos = pos;
    decl->name = arena_strdup(arena, name);
    decl->variants = variants;
    decl->nvariants = nvariants;
    return (AstDecl *)decl;
}

AstDecl *ast_import_decl(Arena *arena, SourcePos pos, const char *module) {
    AstImportDecl *decl = ast_alloc(arena, sizeof(AstImportDecl));
    decl->base.kind = AST_IMPORT_DECL;
    decl->base.pos = pos;
    decl->module = arena_strdup(arena, module);
    return (AstDecl *)decl;
}

/* ------------------------------------------------------------------ */
/* Debug dumps                                                        */
/* ------------------------------------------------------------------ */

void ast_dump_type_syntax(const AstType *type, FILE *out) {
    if (!type) {
        fputs("void", out);
        return;
    }
    switch (type->kind) {
    case AST_TYPE_NAME:
        fputs(type->name, out);
        break;
    case AST_TYPE_PTR:
        fputs("ptr<", out);
        ast_dump_type_syntax(type->elem, out);
        fputc('>', out);
        break;
    case AST_TYPE_ARRAY:
        fputc('[', out);
        ast_dump_type_syntax(type->elem, out);
        fprintf(out, "; %" PRId64 "]", type->array_length);
        break;
    case AST_TYPE_FN:
        fputs("fn(", out);
        for (size_t i = 0; i < type->nparams; i++) {
            if (i) {
                fputs(", ", out);
            }
            ast_dump_type_syntax(type->params[i], out);
        }
        fputc(')', out);
        if (type->ret) {
            fputs(" -> ", out);
            ast_dump_type_syntax(type->ret, out);
        }
        break;
    default:
        fputs("<type?>", out);
        break;
    }
}

static void dump_escaped_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const char *p = value; p && *p; p++) {
        switch (*p) {
        case '\n': fputs("\\n", out); break;
        case '\t': fputs("\\t", out); break;
        case '\r': fputs("\\r", out); break;
        case '"': fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        default:
            if ((unsigned char)*p < 0x20) {
                fprintf(out, "\\x%02X", (unsigned char)*p);
            } else {
                fputc(*p, out);
            }
            break;
        }
    }
    fputc('"', out);
}

void ast_dump_expr(const AstExpr *expr, FILE *out) {
    if (!expr) {
        fputs("<none>", out);
        return;
    }
    switch (expr->kind) {
    case AST_INT_LITERAL:
        fprintf(out, "%" PRId64, ((const AstIntLiteral *)expr)->value);
        break;
    case AST_FLOAT_LITERAL: {
        double value = ((const AstFloatLiteral *)expr)->value;
        if (value == (double)(int64_t)value && value < 1e15 && value > -1e15) {
            fprintf(out, "%.1f", value);
        } else {
            fprintf(out, "%g", value);
        }
        break;
    }
    case AST_STRING_LITERAL:
        dump_escaped_string(out, ((const AstStringLiteral *)expr)->value);
        break;
    case AST_CHAR_LITERAL: {
        int64_t value = ((const AstCharLiteral *)expr)->value;
        if (value >= 0x20 && value < 0x7F && value != '\'' && value != '\\') {
            fprintf(out, "'%c'", (char)value);
        } else {
            fprintf(out, "'\\x%02X'", (unsigned)value);
        }
        break;
    }
    case AST_BOOL_LITERAL:
        fputs(((const AstBoolLiteral *)expr)->value ? "true" : "false", out);
        break;
    case AST_NULL_LITERAL:
        fputs("null", out);
        break;
    case AST_IDENT:
        fputs(((const AstIdent *)expr)->name, out);
        break;
    case AST_UNARY: {
        const AstUnary *unary = (const AstUnary *)expr;
        fprintf(out, "(%s ", unop_name(unary->op));
        ast_dump_expr(unary->operand, out);
        fputc(')', out);
        break;
    }
    case AST_BINARY: {
        const AstBinary *binary = (const AstBinary *)expr;
        fprintf(out, "(%s ", binop_name(binary->op));
        ast_dump_expr(binary->lhs, out);
        fputc(' ', out);
        ast_dump_expr(binary->rhs, out);
        fputc(')', out);
        break;
    }
    case AST_CALL: {
        const AstCall *call = (const AstCall *)expr;
        fputs("(call ", out);
        ast_dump_expr(call->callee, out);
        for (size_t i = 0; i < call->argc; i++) {
            fputc(' ', out);
            ast_dump_expr(call->args[i], out);
        }
        fputc(')', out);
        break;
    }
    case AST_FIELD: {
        const AstField *field = (const AstField *)expr;
        fputs("(field ", out);
        ast_dump_expr(field->object, out);
        fprintf(out, " %s)", field->name);
        break;
    }
    case AST_INDEX: {
        const AstIndex *index = (const AstIndex *)expr;
        fputs("(index ", out);
        ast_dump_expr(index->object, out);
        fputc(' ', out);
        ast_dump_expr(index->index, out);
        fputc(')', out);
        break;
    }
    case AST_CAST: {
        const AstCast *cast = (const AstCast *)expr;
        fputs("(as ", out);
        ast_dump_expr(cast->operand, out);
        fputc(' ', out);
        ast_dump_type_syntax(cast->target_type, out);
        fputc(')', out);
        break;
    }
    case AST_STRUCT_LITERAL: {
        const AstStructLiteral *literal = (const AstStructLiteral *)expr;
        fputs("(struct-literal ", out);
        ast_dump_type_syntax(literal->type_syntax, out);
        for (size_t i = 0; i < literal->count; i++) {
            fputc(' ', out);
            fputc('(', out);
            fputs(literal->field_names[i], out);
            fputc(' ', out);
            ast_dump_expr(literal->field_values[i], out);
            fputc(')', out);
        }
        fputc(')', out);
        break;
    }
    case AST_ARRAY_LITERAL: {
        const AstArrayLiteral *literal = (const AstArrayLiteral *)expr;
        if (literal->is_repeat) {
            fputs("(array-repeat ", out);
            ast_dump_expr(literal->elements[0], out);
            fprintf(out, " %lld)", (long long)literal->repeat_count);
        } else {
            fputs("(array", out);
            for (size_t i = 0; i < literal->count; i++) {
                fputc(' ', out);
                ast_dump_expr(literal->elements[i], out);
            }
            fputc(')', out);
        }
        break;
    }
    case AST_ERROR_EXPR:
        fputs("<error>", out);
        break;
    default:
        fputs("<expr?>", out);
        break;
    }
}

static void dump_indent(FILE *out, int indent) {
    for (int i = 0; i < indent; i++) {
        fputs("  ", out);
    }
}

static void dump_block_body(const AstBlock *block, FILE *out, int indent) {
    for (size_t i = 0; i < block->count; i++) {
        ast_dump_stmt(block->stmts[i], out, indent);
    }
}

void ast_dump_stmt(const AstStmt *stmt, FILE *out, int indent) {
    if (!stmt) {
        return;
    }
    dump_indent(out, indent);
    switch (stmt->kind) {
    case AST_VAR_DECL: {
        const AstVarDecl *decl = (const AstVarDecl *)stmt;
        fprintf(out, "(%s %s", decl->is_mut ? "var" : "let", decl->name);
        if (decl->type_syntax) {
            fputc(' ', out);
            ast_dump_type_syntax(decl->type_syntax, out);
        }
        if (decl->init) {
            fputc(' ', out);
            ast_dump_expr(decl->init, out);
        }
        fputs(")\n", out);
        break;
    }
    case AST_ASSIGN: {
        const AstAssign *assign = (const AstAssign *)stmt;
        fputs("(assign ", out);
        if (assign->op != BIN_NONE) {
            fprintf(out, "%s ", binop_name(assign->op));
        }
        ast_dump_expr(assign->target, out);
        fputc(' ', out);
        ast_dump_expr(assign->value, out);
        fputs(")\n", out);
        break;
    }
    case AST_EXPR_STMT:
        ast_dump_expr(((const AstExprStmt *)stmt)->expr, out);
        fputc('\n', out);
        break;
    case AST_IF: {
        const AstIf *if_stmt = (const AstIf *)stmt;
        fputs("(if ", out);
        ast_dump_expr(if_stmt->cond, out);
        fputs("\n", out);
        if (if_stmt->then_block) {
            dump_indent(out, indent + 1);
            fputs("(then\n", out);
            dump_block_body(if_stmt->then_block, out, indent + 2);
            dump_indent(out, indent + 1);
            fputs(")\n", out);
        }
        if (if_stmt->else_branch) {
            dump_indent(out, indent + 1);
            fputs("(else\n", out);
            ast_dump_stmt(if_stmt->else_branch, out, indent + 2);
            dump_indent(out, indent + 1);
            fputs(")\n", out);
        }
        dump_indent(out, indent);
        fputs(")\n", out);
        break;
    }
    case AST_WHILE: {
        const AstWhile *while_stmt = (const AstWhile *)stmt;
        fputs("(while ", out);
        ast_dump_expr(while_stmt->cond, out);
        fputs("\n", out);
        dump_block_body(while_stmt->body, out, indent + 1);
        dump_indent(out, indent);
        fputs(")\n", out);
        break;
    }
    case AST_FOR: {
        const AstFor *for_stmt = (const AstFor *)stmt;
        fprintf(out, "(for %s ", for_stmt->var);
        ast_dump_expr(for_stmt->start, out);
        fputs(for_stmt->inclusive ? " ..= " : " .. ", out);
        ast_dump_expr(for_stmt->end, out);
        fputs("\n", out);
        dump_block_body(for_stmt->body, out, indent + 1);
        dump_indent(out, indent);
        fputs(")\n", out);
        break;
    }
    case AST_RETURN: {
        const AstReturn *ret = (const AstReturn *)stmt;
        fputs("(return", out);
        if (ret->value) {
            fputc(' ', out);
            ast_dump_expr(ret->value, out);
        }
        fputs(")\n", out);
        break;
    }
    case AST_BREAK:
        fputs("(break)\n", out);
        break;
    case AST_CONTINUE:
        fputs("(continue)\n", out);
        break;
    case AST_BLOCK_STMT: {
        fputs("(block\n", out);
        dump_block_body(((const AstBlockStmt *)stmt)->block, out, indent + 1);
        dump_indent(out, indent);
        fputs(")\n", out);
        break;
    }
    default:
        fputs("<stmt?>\n", out);
        break;
    }
}

void ast_dump_program(const AstProgram *program, FILE *out) {
    fputs("(program\n", out);
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        dump_indent(out, 1);
        switch (decl->kind) {
        case AST_IMPORT_DECL:
            fprintf(out, "(import %s)\n", ((AstImportDecl *)decl)->module);
            break;
        case AST_FN_DECL: {
            AstFnDecl *fn = (AstFnDecl *)decl;
            fprintf(out, "(%sfn %s", fn->is_extern ? "extern " : "", fn->name);
            for (size_t p = 0; p < fn->nparams; p++) {
                fputs(" (", out);
                fputs(fn->params[p].name, out);
                fputc(' ', out);
                ast_dump_type_syntax(fn->params[p].type_syntax, out);
                fputc(')', out);
            }
            if (fn->return_type_syntax) {
                fputs(" -> ", out);
                ast_dump_type_syntax(fn->return_type_syntax, out);
            }
            fputc('\n', out);
            if (fn->body) {
                dump_block_body(fn->body, out, 2);
            }
            dump_indent(out, 1);
            fputs(")\n", out);
            break;
        }
        case AST_STRUCT_DECL: {
            AstStructDecl *st = (AstStructDecl *)decl;
            fprintf(out, "(struct %s", st->name);
            for (size_t f = 0; f < st->nfields; f++) {
                fprintf(out, " (%s ", st->fields[f].name);
                ast_dump_type_syntax(st->fields[f].type_syntax, out);
                fputc(')', out);
            }
            fputs(")\n", out);
            break;
        }
        case AST_ENUM_DECL: {
            AstEnumDecl *en = (AstEnumDecl *)decl;
            fprintf(out, "(enum %s", en->name);
            for (size_t v = 0; v < en->nvariants; v++) {
                fprintf(out, " (%s %" PRId64 ")", en->variants[v].name, en->variants[v].value);
            }
            fputs(")\n", out);
            break;
        }
        default:
            fputs("<decl?>\n", out);
            break;
        }
    }
    fputs(")\n", out);
}
