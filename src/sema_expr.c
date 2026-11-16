#include "sema_internal.h"

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static Type *prim(Sema *sema, TypeKind kind) {
    return type_prim(&sema->types, kind);
}

/* An address-of expression counts as a use of the base variables, so that
 * `var x = 1; let p = &x;` does not warn about an unused variable. */
static void mark_used(AstExpr *expr) {
    if (!expr) {
        return;
    }
    switch (expr->kind) {
    case AST_IDENT: {
        AstIdent *ident = (AstIdent *)expr;
        if (ident->symbol) {
            ident->symbol->read = true;
        }
        break;
    }
    case AST_FIELD:
        mark_used(((AstField *)expr)->object);
        break;
    case AST_INDEX: {
        AstIndex *index = (AstIndex *)expr;
        mark_used(index->object);
        mark_used(index->index);
        break;
    }
    default:
        break;
    }
}

static void set_value(ExprValue *value, Type *type) {
    value->type = type;
    value->is_lvalue = false;
    value->is_mutable = false;
}

static bool is_numeric_expected(Type *type) {
    return type && (type_is_integer(type) || type_is_float(type));
}

static void expr_type_string(Type *type, char *buf, size_t len) {
    type_to_string_buf(type, buf, len);
}

static void fmt_function_signature(FunctionInfo *fn, char *buf, size_t len) {
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "%s(", fn->name);
    for (size_t i = 0; i < fn->nparams; i++) {
        if (i) {
            sb_append(&sb, ", ");
        }
        type_to_string_buf(fn->param_types[i], buf, len);
        sb_append(&sb, buf);
    }
    sb_append(&sb, ")");
    if (fn->return_type->kind != TY_VOID) {
        sb_append(&sb, " -> ");
        type_to_string_buf(fn->return_type, buf, len);
        sb_append(&sb, buf);
    }
    snprintf(buf, len, "%s", sb.data ? sb.data : "");
    sb_free(&sb);
}

static void fmt_builtin_signature(const BuiltinFn *fn, char *buf, size_t len) {
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "%s(", fn->name);
    for (size_t i = 0; i < fn->nparams; i++) {
        if (i) {
            sb_append(&sb, ", ");
        }
        sb_append(&sb, stdlib_spec_name(fn->params[i]));
    }
    sb_append(&sb, ")");
    if (fn->ret != BSPEC_VOID) {
        sb_append(&sb, " -> ");
        sb_append(&sb, stdlib_spec_name(fn->ret));
    }
    snprintf(buf, len, "%s", sb.data ? sb.data : "");
    sb_free(&sb);
}

/* ------------------------------------------------------------------ */
/* Constant folding for integer expressions                           */
/* ------------------------------------------------------------------ */

bool sema_eval_const_int(AstExpr *expr, int64_t *value, uint64_t *uvalue) {
    if (!expr) {
        return false;
    }
    switch (expr->kind) {
    case AST_INT_LITERAL: {
        AstIntLiteral *lit = (AstIntLiteral *)expr;
        *value = lit->value;
        *uvalue = lit->uvalue;
        return true;
    }
    case AST_UNARY: {
        AstUnary *unary = (AstUnary *)expr;
        int64_t v;
        uint64_t u;
        if (!sema_eval_const_int(unary->operand, &v, &u)) {
            return false;
        }
        switch (unary->op) {
        case UN_NEG:
            if (u > (uint64_t)INT64_MAX + 1) {
                return false;
            }
            *uvalue = (uint64_t)0 - u;
            *value = (int64_t)*uvalue;
            return true;
        case UN_BNOT:
            *uvalue = ~u;
            *value = (int64_t)*uvalue;
            return true;
        default:
            return false;
        }
    }
    case AST_BINARY: {
        AstBinary *binary = (AstBinary *)expr;
        int64_t lv, rv;
        uint64_t lu, ru;
        if (!sema_eval_const_int(binary->lhs, &lv, &lu) ||
            !sema_eval_const_int(binary->rhs, &rv, &ru)) {
            return false;
        }
        switch (binary->op) {
        case BIN_ADD:
            *uvalue = lu + ru;
            *value = (int64_t)*uvalue;
            return true;
        case BIN_SUB:
            *uvalue = lu - ru;
            *value = (int64_t)*uvalue;
            return true;
        case BIN_MUL:
            *uvalue = lu * ru;
            *value = (int64_t)*uvalue;
            return true;
        case BIN_DIV:
            if (ru == 0) {
                return false;
            }
            if (rv == -1) {
                *uvalue = (uint64_t)0 - lu;
                *value = (int64_t)*uvalue;
                return true;
            }
            *value = lv / rv;
            *uvalue = (uint64_t)*value;
            return true;
        case BIN_MOD:
            if (ru == 0) {
                return false;
            }
            if (rv == -1) {
                *value = 0;
                *uvalue = 0;
                return true;
            }
            *value = lv % rv;
            *uvalue = (uint64_t)*value;
            return true;
        case BIN_AND:
            *uvalue = lu & ru;
            *value = (int64_t)*uvalue;
            return true;
        case BIN_OR:
            *uvalue = lu | ru;
            *value = (int64_t)*uvalue;
            return true;
        case BIN_XOR:
            *uvalue = lu ^ ru;
            *value = (int64_t)*uvalue;
            return true;
        case BIN_SHL:
            if (ru >= 64) {
                return false;
            }
            *uvalue = lu << ru;
            *value = (int64_t)*uvalue;
            return true;
        case BIN_SHR:
            if (ru >= 64) {
                return false;
            }
            *value = lv >> ru;
            *uvalue = (uint64_t)*value;
            return true;
        default:
            return false;
        }
    }
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Builtin argument scoring                                           */
/* ------------------------------------------------------------------ */

static bool signed_value_fits(Type *target, int64_t value) {
    switch (target->kind) {
    case TY_I8: return value >= -128 && value <= 127;
    case TY_I16: return value >= -32768 && value <= 32767;
    case TY_I32: return value >= -2147483648LL && value <= 2147483647LL;
    case TY_I64: return true;
    default: return false;
    }
}

static int score_integer_spec(Sema *sema, BuiltinTypeSpec spec, AstExpr *arg, Type *arg_type) {
    int64_t value = 0;
    uint64_t uvalue = 0;
    bool constant = sema_eval_const_int(arg, &value, &uvalue);
    if (!constant) {
        /* Non-constant expression of untyped integer nature: prefer i32. */
        switch (spec) {
        case BSPEC_I32: return 3;
        case BSPEC_I64: return 2;
        case BSPEC_U64: return 1;
        case BSPEC_ANY_INT: return 2;
        case BSPEC_ANY_NUMERIC: return 2;
        default: return 1;
        }
    }
    Type *target = stdlib_spec_type(&sema->types, spec);
    if (!target) {
        return -1;
    }
    bool fits;
    if (value < 0 && type_is_signed_int(target)) {
        /* A negated literal keeps its magnitude in uvalue; check the signed
         * value directly so `-42` fits i64. */
        fits = signed_value_fits(target, value);
    } else {
        fits = sema_int_literal_fits(target, value, uvalue);
    }
    if (!fits) {
        return -1;
    }
    switch (spec) {
    case BSPEC_I32: return 3;
    case BSPEC_I64: return 2;
    case BSPEC_I8:
    case BSPEC_I16:
    case BSPEC_U8:
    case BSPEC_U16:
    case BSPEC_U32: return 1;
    case BSPEC_U64: return 2;
    case BSPEC_ANY_INT: return 2;
    case BSPEC_ANY_NUMERIC: return 2;
    default: return -1;
    }
    RYN_UNUSED(arg_type);
}

int sema_builtin_arg_score(Sema *sema, BuiltinTypeSpec spec, AstExpr *arg, const Type *arg_type) {
    if (!arg || !arg_type) {
        return -1;
    }
    if (type_is_error(arg_type)) {
        return 3;
    }
    if (arg_type->kind == TY_UNTYPED_INT) {
        return score_integer_spec(sema, spec, arg, (Type *)arg_type);
    }
    if (arg_type->kind == TY_UNTYPED_FLOAT) {
        switch (spec) {
        case BSPEC_F64: return 3;
        case BSPEC_F32: return 2;
        case BSPEC_ANY_NUMERIC: return 2;
        default: return -1;
        }
    }
    int score = 0;
    if (stdlib_spec_matches(spec, arg_type, &score)) {
        return score;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */

static void check_expr_internal(Sema *sema, AstExpr *expr, Type *expected, ExprValue *out);
static void check_call(Sema *sema, AstCall *call, Type *expected, ExprValue *out);
static void check_field(Sema *sema, AstField *field, Type *expected, ExprValue *out);
static void check_binary(Sema *sema, AstBinary *binary, Type *expected, ExprValue *out);

/* ------------------------------------------------------------------ */
/* Literals and identifiers                                           */
/* ------------------------------------------------------------------ */

static void check_int_literal(Sema *sema, AstIntLiteral *lit, Type *expected, ExprValue *out) {
    if (is_numeric_expected(expected)) {
        if (!sema_int_literal_fits(expected, lit->value, lit->uvalue)) {
            char type_buf[64];
            expr_type_string(expected, type_buf, sizeof(type_buf));
            sema_error(sema, lit->base.pos, 1, E_LITERAL_RANGE,
                       "integer literal does not fit in '%s'", type_buf);
        }
        set_value(out, expected);
        return;
    }
    if (expected && expected->kind == TY_ERROR) {
        set_value(out, expected);
        return;
    }
    set_value(out, prim(sema, TY_UNTYPED_INT));
}

static void check_float_literal(Sema *sema, AstFloatLiteral *lit, Type *expected, ExprValue *out) {
    if (expected && (expected->kind == TY_F32 || expected->kind == TY_F64)) {
        set_value(out, expected);
        return;
    }
    if (expected && expected->kind == TY_ERROR) {
        set_value(out, expected);
        return;
    }
    set_value(out, prim(sema, TY_UNTYPED_FLOAT));
    RYN_UNUSED(lit);
}

static void check_ident(Sema *sema, AstIdent *ident, ExprValue *out) {
    SemaSymbol *symbol = sema_scope_lookup(sema, ident->name);
    if (symbol && symbol->kind == SYM_MODULE) {
        sema_error_note(sema, ident->base.pos, (int)strlen(ident->name), E_NOT_CALLABLE,
                        "use '%s.member' to access functions and constants",
                        "module '%s' cannot be used as a value", ident->name);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    if (symbol) {
        ident->symbol = symbol;
        if (!sema->lvalue_mode) {
            symbol->read = true;
            if (!symbol->initialized) {
                sema_error_note(sema, ident->base.pos, (int)strlen(ident->name), E_UNINITIALIZED,
                                "assign a value before reading the variable",
                                "variable '%s' is used before it is initialized", ident->name);
            }
        }
        set_value(out, symbol->type);
        out->is_lvalue = true;
        out->is_mutable = symbol->is_mutable;
        return;
    }
    FunctionInfo *fn = sema_lookup_function(sema, ident->name);
    if (fn) {
        ident->refers_to_function = true;
        ident->function = fn;
        set_value(out, fn->fn_type);
        return;
    }
    if (sema_lookup_struct(sema, ident->name) || sema_lookup_enum(sema, ident->name)) {
        sema_error_note(sema, ident->base.pos, (int)strlen(ident->name), E_UNDEFINED_VARIABLE,
                        "type names are not values", "'%s' is a type, not a value",
                        ident->name);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    sema_error_note(sema, ident->base.pos, (int)strlen(ident->name), E_UNDEFINED_VARIABLE,
                    "variables must be declared before they are used",
                    "undefined variable '%s'", ident->name);
    set_value(out, prim(sema, TY_ERROR));
}

/* ------------------------------------------------------------------ */
/* Unary operators                                                    */
/* ------------------------------------------------------------------ */

static void check_unary(Sema *sema, AstUnary *unary, Type *expected, ExprValue *out) {
    char actual_buf[96];
    ExprValue operand;
    switch (unary->op) {
    case UN_NEG: {
        if (unary->operand->kind == AST_INT_LITERAL) {
            AstIntLiteral *lit = (AstIntLiteral *)unary->operand;
            if (expected && type_is_signed_int(expected) &&
                lit->uvalue <= (uint64_t)INT64_MAX + 1) {
                /* Check the negated value against the expected type, so that
                 * i8 = -128 and i64 = -9223372036854775808 are accepted even
                 * though the positive magnitude does not fit. The IR folds
                 * this negation into a single constant. */
                uint64_t negated = (uint64_t)0 - lit->uvalue;
                if (!sema_int_literal_fits(expected, (int64_t)negated, 0)) {
                    char type_buf[64];
                    expr_type_string(expected, type_buf, sizeof(type_buf));
                    sema_error(sema, lit->base.pos, 1, E_LITERAL_RANGE,
                               "integer literal does not fit in '%s'", type_buf);
                }
                lit->base.type = expected;
                set_value(out, expected);
                return;
            }
            if (lit->uvalue == (uint64_t)INT64_MAX + 1) {
                /* -9223372036854775808 is exactly INT64_MIN. */
                Type *i64 = prim(sema, TY_I64);
                lit->base.type = i64;
                set_value(out, i64);
                return;
            }
        }
        check_expr_internal(sema, unary->operand, is_numeric_expected(expected) ? expected : NULL,
                            &operand);
        if (type_is_untyped(operand.type)) {
            set_value(out, operand.type);
            return;
        }
        if (type_is_numeric(operand.type)) {
            set_value(out, operand.type);
            return;
        }
        expr_type_string(operand.type, actual_buf, sizeof(actual_buf));
        if (type_is_error(operand.type)) {
            set_value(out, operand.type);
            return;
        }
        sema_error(sema, unary->base.pos, 1, E_BAD_OPERATOR,
                   "unary '-' requires a number, found '%s'", actual_buf);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    case UN_NOT: {
        check_expr_internal(sema, unary->operand, prim(sema, TY_BOOL), &operand);
        if (!type_is_error(operand.type) && operand.type->kind != TY_BOOL) {
            expr_type_string(operand.type, actual_buf, sizeof(actual_buf));
            sema_error_note(sema, unary->base.pos, 1, E_BAD_OPERATOR,
                            "use a comparison such as 'x == 0' to obtain a bool",
                            "logical negation requires a bool, found '%s'", actual_buf);
        }
        set_value(out, prim(sema, TY_BOOL));
        return;
    }
    case UN_BNOT: {
        check_expr_internal(sema, unary->operand, NULL, &operand);
        if (type_is_untyped(operand.type)) {
            set_value(out, operand.type);
            return;
        }
        if (type_is_integer(operand.type)) {
            set_value(out, operand.type);
            return;
        }
        if (type_is_error(operand.type)) {
            set_value(out, operand.type);
            return;
        }
        expr_type_string(operand.type, actual_buf, sizeof(actual_buf));
        sema_error(sema, unary->base.pos, 1, E_BAD_OPERATOR,
                   "bitwise '~' requires an integer, found '%s'", actual_buf);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    case UN_ADDR: {
        bool saved_lvalue_mode = sema->lvalue_mode;
        sema->lvalue_mode = true;
        check_expr_internal(sema, unary->operand, NULL, &operand);
        sema->lvalue_mode = saved_lvalue_mode;
        mark_used(unary->operand);
        if (type_is_error(operand.type)) {
            set_value(out, operand.type);
            return;
        }
        if (!operand.is_lvalue) {
            sema_error_note(sema, unary->base.pos, 1, E_INVALID_ADDRESS_OF,
                            "'&' can only be applied to variables, fields or array elements",
                            "cannot take the address of this expression");
            set_value(out, prim(sema, TY_ERROR));
            return;
        }
        if (!operand.is_mutable) {
            sema_error_note(sema, unary->base.pos, 1, E_INVALID_ADDRESS_OF,
                            "declare the variable with 'var' if you need a mutable pointer",
                            "cannot take a mutable pointer to an immutable value");
            set_value(out, prim(sema, TY_ERROR));
            return;
        }
        set_value(out, type_ptr(sema->arena, operand.type));
        out->is_lvalue = false;
        return;
    }
    case UN_DEREF: {
        check_expr_internal(sema, unary->operand, NULL, &operand);
        if (type_is_error(operand.type)) {
            set_value(out, operand.type);
            return;
        }
        if (operand.type->kind == TY_PTR) {
            set_value(out, operand.type->as.ptr.elem);
            out->is_lvalue = true;
            out->is_mutable = true;
            return;
        }
        expr_type_string(operand.type, actual_buf, sizeof(actual_buf));
        sema_error_note(sema, unary->base.pos, 1, E_DEREF_NON_POINTER,
                        "only ptr<T> values can be dereferenced",
                        "cannot dereference a value of type '%s'", actual_buf);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    default:
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Binary operators                                                   */
/* ------------------------------------------------------------------ */

static bool pointer_types_compatible(Type *a, Type *b) {
    if (!a || !b) {
        return false;
    }
    if (a->kind == TY_NULL && b->kind == TY_PTR) {
        return true;
    }
    if (b->kind == TY_NULL && a->kind == TY_PTR) {
        return true;
    }
    if (a->kind != TY_PTR || b->kind != TY_PTR) {
        return false;
    }
    if (a->as.ptr.elem->kind == TY_VOID || b->as.ptr.elem->kind == TY_VOID) {
        return true;
    }
    return type_eq(a->as.ptr.elem, b->as.ptr.elem);
}

/* Makes both operands have the same numeric type where possible.
 * Returns false (after reporting) when the types are incompatible. */
static bool unify_numeric_operands(Sema *sema, AstBinary *binary, ExprValue *lhs, ExprValue *rhs,
                                   const char *op_name) {
    if (type_is_error(lhs->type) || type_is_error(rhs->type)) {
        return true;
    }
    if (type_is_untyped(lhs->type) && type_is_untyped(rhs->type)) {
        if (lhs->type->kind == TY_UNTYPED_FLOAT || rhs->type->kind == TY_UNTYPED_FLOAT) {
            Type *f64 = prim(sema, TY_F64);
            sema_materialize_default(sema, binary->lhs, f64);
            sema_materialize_default(sema, binary->rhs, f64);
            lhs->type = f64;
            rhs->type = f64;
        }
        return true;
    }
    if (type_is_untyped(lhs->type)) {
        sema_coerce(sema, binary->lhs, lhs, rhs->type, "arithmetic operand");
        return true;
    }
    if (type_is_untyped(rhs->type)) {
        sema_coerce(sema, binary->rhs, rhs, lhs->type, "arithmetic operand");
        return true;
    }
    if (type_eq(lhs->type, rhs->type)) {
        return true;
    }
    char left_buf[64];
    char right_buf[64];
    expr_type_string(lhs->type, left_buf, sizeof(left_buf));
    expr_type_string(rhs->type, right_buf, sizeof(right_buf));
    sema_error_note(sema, binary->base.pos, 1, E_TYPE_MISMATCH,
                    "arithmetic does not implicitly mix types; convert one operand with 'as'",
                    "operator '%s' cannot combine '%s' and '%s'", op_name, left_buf, right_buf);
    return false;
}

static void check_binary(Sema *sema, AstBinary *binary, Type *expected, ExprValue *out) {
    char left_buf[64];
    char right_buf[64];
    ExprValue lhs;
    ExprValue rhs;

    /* Logical operators are short-circuiting and require bools. */
    if (binop_is_logical(binary->op)) {
        check_expr_internal(sema, binary->lhs, prim(sema, TY_BOOL), &lhs);
        check_expr_internal(sema, binary->rhs, prim(sema, TY_BOOL), &rhs);
        if (!type_is_error(lhs.type) && lhs.type->kind != TY_BOOL) {
            expr_type_string(lhs.type, left_buf, sizeof(left_buf));
            sema_error(sema, binary->lhs->pos, 1, E_BAD_OPERATOR,
                       "'%s' requires bool operands, found '%s'", binop_name(binary->op),
                       left_buf);
        }
        if (!type_is_error(rhs.type) && rhs.type->kind != TY_BOOL) {
            expr_type_string(rhs.type, right_buf, sizeof(right_buf));
            sema_error(sema, binary->rhs->pos, 1, E_BAD_OPERATOR,
                       "'%s' requires bool operands, found '%s'", binop_name(binary->op),
                       right_buf);
        }
        set_value(out, prim(sema, TY_BOOL));
        return;
    }

    check_expr_internal(sema, binary->lhs, is_numeric_expected(expected) ? expected : NULL, &lhs);
    check_expr_internal(sema, binary->rhs, NULL, &rhs);

    /* Comparison of strings by content. */
    if (binary->op == BIN_EQ || binary->op == BIN_NE) {
        if ((lhs.type->kind == TY_STR && rhs.type->kind == TY_STR) ||
            (lhs.type->kind == TY_STR && type_is_untyped(rhs.type)) ||
            (rhs.type->kind == TY_STR && type_is_untyped(lhs.type))) {
            set_value(out, prim(sema, TY_BOOL));
            return;
        }
    }

    if (binop_is_comparison(binary->op)) {
        if (lhs.type->kind == TY_PTR || rhs.type->kind == TY_PTR ||
            lhs.type->kind == TY_NULL || rhs.type->kind == TY_NULL) {
            if (!pointer_types_compatible(lhs.type, rhs.type)) {
                expr_type_string(lhs.type, left_buf, sizeof(left_buf));
                expr_type_string(rhs.type, right_buf, sizeof(right_buf));
                sema_error(sema, binary->base.pos, 1, E_TYPE_MISMATCH,
                           "cannot compare pointers of type '%s' and '%s'", left_buf, right_buf);
            }
            if (binary->op != BIN_EQ && binary->op != BIN_NE) {
                sema_error_note(sema, binary->base.pos, 1, E_BAD_OPERATOR,
                                "convert pointers to u64 with 'as' to compare addresses",
                                "ordering comparison of pointers is not supported");
            }
            set_value(out, prim(sema, TY_BOOL));
            return;
        }
        if (!unify_numeric_operands(sema, binary, &lhs, &rhs, binop_name(binary->op))) {
            set_value(out, prim(sema, TY_ERROR));
            return;
        }
        bool lnum = type_is_numeric(lhs.type) || type_is_untyped(lhs.type);
        bool rnum = type_is_numeric(rhs.type) || type_is_untyped(rhs.type);
        bool lbool = lhs.type->kind == TY_BOOL;
        bool rbool = rhs.type->kind == TY_BOOL;
        bool lenum = lhs.type->kind == TY_ENUM && rhs.type->kind == TY_ENUM &&
                     type_eq(lhs.type, rhs.type);
        if (type_is_untyped(lhs.type) && type_is_untyped(rhs.type)) {
            Type *chosen = lhs.type->kind == TY_UNTYPED_FLOAT || rhs.type->kind == TY_UNTYPED_FLOAT
                               ? prim(sema, TY_F64)
                               : prim(sema, TY_I32);
            sema_materialize_default(sema, binary->lhs, chosen);
            sema_materialize_default(sema, binary->rhs, chosen);
            lhs.type = chosen;
            rhs.type = chosen;
        } else if (!((lnum && rnum) || (lbool && rbool) || lenum ||
                     (lhs.type->kind == TY_CHAR && rhs.type->kind == TY_CHAR))) {
            expr_type_string(lhs.type, left_buf, sizeof(left_buf));
            expr_type_string(rhs.type, right_buf, sizeof(right_buf));
            if (!type_is_error(lhs.type) && !type_is_error(rhs.type)) {
                sema_error(sema, binary->base.pos, 1, E_TYPE_MISMATCH,
                           "cannot compare '%s' with '%s'", left_buf, right_buf);
            }
        }
        set_value(out, prim(sema, TY_BOOL));
        return;
    }

    /* Bitwise and shift operators. */
    if (binary->op == BIN_SHL || binary->op == BIN_SHR) {
        if (!unify_numeric_operands(sema, binary, &lhs, &rhs, binop_name(binary->op))) {
            set_value(out, prim(sema, TY_ERROR));
            return;
        }
        if (!type_is_error(lhs.type) && !type_is_integer(lhs.type) && !type_is_untyped(lhs.type)) {
            expr_type_string(lhs.type, left_buf, sizeof(left_buf));
            sema_error(sema, binary->base.pos, 1, E_BAD_OPERATOR,
                       "shift requires an integer, found '%s'", left_buf);
        }
        if (!type_is_error(rhs.type) && !type_is_integer(rhs.type) && !type_is_untyped(rhs.type)) {
            expr_type_string(rhs.type, right_buf, sizeof(right_buf));
            sema_error(sema, binary->base.pos, 1, E_BAD_OPERATOR,
                       "shift amount must be an integer, found '%s'", right_buf);
        }
        int64_t shift_value;
        uint64_t shift_uvalue;
        if (sema_eval_const_int(binary->rhs, &shift_value, &shift_uvalue) &&
            (shift_value < 0 || shift_uvalue >= 64)) {
            sema_error(sema, binary->rhs->pos, 1, E_OUT_OF_RANGE,
                       "shift amount must be between 0 and 63");
        }
        if (type_is_untyped(rhs.type)) {
            sema_default_expr_type(sema, binary->rhs, &rhs);
        }
        set_value(out, lhs.type);
        return;
    }
    if (binary->op == BIN_AND || binary->op == BIN_OR || binary->op == BIN_XOR) {
        if (!unify_numeric_operands(sema, binary, &lhs, &rhs, binop_name(binary->op))) {
            set_value(out, prim(sema, TY_ERROR));
            return;
        }
        if (!type_is_error(lhs.type) && !type_is_integer(lhs.type) && !type_is_untyped(lhs.type)) {
            expr_type_string(lhs.type, left_buf, sizeof(left_buf));
            sema_error(sema, binary->base.pos, 1, E_BAD_OPERATOR,
                       "bitwise operators require integers, found '%s'", left_buf);
        }
        set_value(out, lhs.type);
        return;
    }

    /* Arithmetic. */
    if (lhs.type->kind == TY_STR || rhs.type->kind == TY_STR) {
        sema_error_note(sema, binary->base.pos, 1, E_BAD_OPERATOR,
                        "strings are immutable; use str.concat(a, b)",
                        "operator '%s' is not defined for strings", binop_name(binary->op));
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    if (lhs.type->kind == TY_PTR || rhs.type->kind == TY_PTR) {
        sema_error_note(sema, binary->base.pos, 1, E_BAD_OPERATOR,
                        "index a pointer with p[i] or use the mem module",
                        "pointer arithmetic is not supported in Ryn 0.1");
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    if (!unify_numeric_operands(sema, binary, &lhs, &rhs, binop_name(binary->op))) {
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    if (!type_is_error(lhs.type) && !type_is_numeric(lhs.type) && !type_is_untyped(lhs.type)) {
        expr_type_string(lhs.type, left_buf, sizeof(left_buf));
        sema_error(sema, binary->base.pos, 1, E_BAD_OPERATOR,
                   "operator '%s' requires numbers, found '%s'", binop_name(binary->op), left_buf);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    if (binary->op == BIN_MOD && !type_is_error(lhs.type) && !type_is_integer(lhs.type) &&
        !type_is_untyped(lhs.type)) {
        sema_error(sema, binary->base.pos, 1, E_BAD_OPERATOR,
                   "'%%' requires integer operands");
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    if ((binary->op == BIN_DIV || binary->op == BIN_MOD) && type_is_integer(lhs.type)) {
        int64_t divisor;
        uint64_t divisor_u;
        if (sema_eval_const_int(binary->rhs, &divisor, &divisor_u) && divisor == 0) {
            sema_warn(sema, binary->rhs->pos, 1, W_DIVISION_BY_ZERO,
                      "division by zero is undefined behaviour");
        }
    }
    set_value(out, lhs.type);
}

/* ------------------------------------------------------------------ */
/* Field access                                                       */
/* ------------------------------------------------------------------ */

static void module_member_note(const BuiltinModule *module, char *buf, size_t len) {
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "module '%s' provides:", module->name);
    size_t written = 0;
    for (size_t i = 0; i < module->nfunctions && written < 8; i++) {
        bool duplicate = false;
        for (size_t k = 0; k < i; k++) {
            if (strcmp(module->functions[k].name, module->functions[i].name) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            sb_appendf(&sb, " %s", module->functions[i].name);
            written++;
        }
    }
    for (size_t i = 0; i < module->nconstants && written < 10; i++) {
        sb_appendf(&sb, " %s", module->constants[i].name);
        written++;
    }
    snprintf(buf, len, "%s", sb.data ? sb.data : "");
    sb_free(&sb);
}

static void check_module_member(Sema *sema, AstField *field, SemaSymbol *module_symbol,
                                ExprValue *out) {
    const BuiltinModule *module = module_symbol->module;
    field->is_module = true;
    for (size_t i = 0; i < module->nconstants; i++) {
        if (strcmp(module->constants[i].name, field->name) == 0) {
            field->constant = &module->constants[i];
            Type *type = stdlib_spec_type(&sema->types, module->constants[i].type);
            set_value(out, type);
            return;
        }
    }
    const BuiltinFn *candidates[32];
    size_t count = stdlib_candidates(module, field->name, candidates, 32);
    if (count > 0) {
        field->builtin = candidates[0];
        sema_error_note(sema, field->base.pos, (int)strlen(field->name), E_NOT_CALLABLE,
                        "builtin functions can only be called directly",
                        "builtin function '%s.%s' must be called", module->name, field->name);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    char note[512];
    module_member_note(module, note, sizeof(note));
    sema_error_note(sema, field->base.pos, (int)strlen(field->name), E_UNKNOWN_FIELD, note,
                    "module '%s' has no member '%s'", module->name, field->name);
    set_value(out, prim(sema, TY_ERROR));
}

static void check_field(Sema *sema, AstField *field, Type *expected, ExprValue *out) {
    RYN_UNUSED(expected);
    char type_buf[96];
    field->field_index = -1;

    if (field->object->kind == AST_IDENT) {
        AstIdent *object_ident = (AstIdent *)field->object;
        SemaSymbol *symbol = sema_scope_lookup(sema, object_ident->name);
        if (symbol && symbol->kind == SYM_MODULE) {
            check_module_member(sema, field, symbol, out);
            return;
        }
        if (!symbol) {
            EnumInfo *enum_info = sema_lookup_enum(sema, object_ident->name);
            if (enum_info) {
                for (size_t i = 0; i < enum_info->nvariants; i++) {
                    if (strcmp(enum_info->variants[i].name, field->name) == 0) {
                        field->enum_info = enum_info;
                        field->variant_index = (int)i;
                        set_value(out, enum_info->type);
                        return;
                    }
                }
                sema_error_note(sema, field->base.pos, (int)strlen(field->name), E_UNKNOWN_FIELD,
                                "enum variants are written as 'EnumName.Variant'",
                                "enum '%s' has no variant '%s'", enum_info->name, field->name);
                set_value(out, prim(sema, TY_ERROR));
                return;
            }
            if (sema_lookup_struct(sema, object_ident->name)) {
                sema_error_note(sema, field->base.pos, (int)strlen(field->name), E_NOT_A_STRUCT,
                                "struct fields belong to values; create one with a struct literal",
                                "'%s' is a struct type, not a value", object_ident->name);
                set_value(out, prim(sema, TY_ERROR));
                return;
            }
        }
    }

    ExprValue object;
    check_expr_internal(sema, field->object, NULL, &object);
    if (type_is_error(object.type)) {
        set_value(out, object.type);
        return;
    }
    Type *struct_type = object.type;
    bool object_lvalue = object.is_lvalue;
    bool object_mutable = object.is_mutable;
    if (struct_type->kind == TY_PTR && struct_type->as.ptr.elem->kind == TY_STRUCT) {
        struct_type = struct_type->as.ptr.elem;
        object_lvalue = true;
        object_mutable = true;
    }
    if (struct_type->kind != TY_STRUCT) {
        expr_type_string(object.type, type_buf, sizeof(type_buf));
        sema_error_note(sema, field->base.pos, (int)strlen(field->name), E_NOT_A_STRUCT,
                        "only struct values have fields",
                        "cannot access field '%s' on a value of type '%s'", field->name,
                        type_buf);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    StructInfo *info = struct_type->as.strukt;
    for (size_t i = 0; i < info->nfields; i++) {
        if (strcmp(info->fields[i].name, field->name) == 0) {
            field->field_index = (int)i;
            set_value(out, info->fields[i].type);
            out->is_lvalue = object_lvalue;
            out->is_mutable = object_mutable;
            return;
        }
    }
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; i < info->nfields; i++) {
        if (i) {
            sb_append(&sb, ", ");
        }
        sb_append(&sb, info->fields[i].name);
    }
    char note[512];
    snprintf(note, sizeof(note), "struct '%s' has fields: %s", info->name, sb.data ? sb.data : "");
    sb_free(&sb);
    sema_error_note(sema, field->base.pos, (int)strlen(field->name), E_UNKNOWN_FIELD, note,
                    "struct '%s' has no field '%s'", info->name, field->name);
    set_value(out, prim(sema, TY_ERROR));
}

/* ------------------------------------------------------------------ */
/* Indexing                                                           */
/* ------------------------------------------------------------------ */

static void check_index(Sema *sema, AstIndex *index, ExprValue *out) {
    char type_buf[96];
    ExprValue object;
    check_expr_internal(sema, index->object, NULL, &object);
    if (type_is_error(object.type)) {
        set_value(out, object.type);
        return;
    }
    ExprValue index_value;
    bool saved_lvalue_mode = sema->lvalue_mode;
    sema->lvalue_mode = false;
    check_expr_internal(sema, index->index, NULL, &index_value);
    sema->lvalue_mode = saved_lvalue_mode;
    if (type_is_untyped(index_value.type)) {
        sema_default_expr_type(sema, index->index, &index_value);
    }
    if (!type_is_error(index_value.type) && !type_is_integer(index_value.type)) {
        expr_type_string(index_value.type, type_buf, sizeof(type_buf));
        sema_error(sema, index->index->pos, 1, E_BAD_INDEX,
                   "array index must be an integer, found '%s'", type_buf);
    }
    if (type_is_integer(index_value.type)) {
        int64_t value;
        uint64_t uvalue;
        if (sema_eval_const_int(index->index, &value, &uvalue) && value < 0) {
            sema_error(sema, index->index->pos, 1, E_OUT_OF_RANGE,
                       "array index cannot be negative");
        }
    }
    if (object.type->kind == TY_ARRAY) {
        if (type_is_integer(index_value.type)) {
            int64_t value;
            uint64_t uvalue;
            if (sema_eval_const_int(index->index, &value, &uvalue) &&
                uvalue >= (uint64_t)object.type->as.array.length) {
                sema_error(sema, index->index->pos, 1, E_OUT_OF_RANGE,
                           "index %lld is out of bounds for an array of length %lld",
                           (long long)value, (long long)object.type->as.array.length);
            }
        }
        set_value(out, object.type->as.array.elem);
        out->is_lvalue = object.is_lvalue;
        out->is_mutable = object.is_mutable;
        return;
    }
    if (object.type->kind == TY_PTR) {
        set_value(out, object.type->as.ptr.elem);
        out->is_lvalue = true;
        out->is_mutable = true;
        return;
    }
    if (object.type->kind == TY_STR) {
        set_value(out, prim(sema, TY_CHAR));
        return;
    }
    expr_type_string(object.type, type_buf, sizeof(type_buf));
    sema_error_note(sema, index->base.pos, 1, E_BAD_INDEX,
                    "only arrays, pointers and strings can be indexed",
                    "cannot index a value of type '%s'", type_buf);
    set_value(out, prim(sema, TY_ERROR));
}

/* ------------------------------------------------------------------ */
/* Casts                                                              */
/* ------------------------------------------------------------------ */

static bool cast_allowed(Type *from, Type *to) {
    if (type_is_error(from) || type_is_error(to)) {
        return true;
    }
    if (type_eq(from, to)) {
        return true;
    }
    if (from->kind == TY_VOID || to->kind == TY_VOID) {
        return false;
    }
    if (type_is_numeric(from) && type_is_numeric(to)) {
        return true;
    }
    if (from->kind == TY_BOOL && (type_is_numeric(to) || to->kind == TY_PTR)) {
        return true;
    }
    if (to->kind == TY_BOOL && (type_is_numeric(from) || from->kind == TY_PTR)) {
        return true;
    }
    if (from->kind == TY_CHAR && (type_is_numeric(to) || to->kind == TY_PTR || to->kind == TY_CHAR)) {
        return true;
    }
    if (to->kind == TY_CHAR && (type_is_numeric(from) || from->kind == TY_PTR)) {
        return true;
    }
    if (from->kind == TY_PTR && (to->kind == TY_PTR || type_is_integer(to) || to->kind == TY_BOOL)) {
        return true;
    }
    if (type_is_integer(from) && to->kind == TY_PTR) {
        return true;
    }
    if (from->kind == TY_NULL && to->kind == TY_PTR) {
        return true;
    }
    if (from->kind == TY_ENUM && (type_is_integer(to) || to->kind == TY_F64 || to->kind == TY_F32)) {
        return true;
    }
    if (type_is_integer(from) && to->kind == TY_ENUM) {
        return true;
    }
    return false;
}

static void check_cast(Sema *sema, AstCast *cast, ExprValue *out) {
    Type *target = sema_resolve_type(sema, cast->target_type);
    if (target->kind == TY_VOID) {
        sema_error(sema, cast->base.pos, 1, E_BAD_CAST, "cannot cast to void");
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    ExprValue operand;
    check_expr_internal(sema, cast->operand,
                        (type_is_numeric(target) || target->kind == TY_BOOL) ? target : NULL,
                        &operand);
    if (type_is_untyped(operand.type)) {
        sema_default_expr_type(sema, cast->operand, &operand);
    }
    if (!type_is_error(operand.type) && !cast_allowed(operand.type, target)) {
        char from_buf[64];
        char to_buf[64];
        expr_type_string(operand.type, from_buf, sizeof(from_buf));
        expr_type_string(target, to_buf, sizeof(to_buf));
        sema_error_note(sema, cast->base.pos, 1, E_BAD_CAST,
                        "casts are allowed between numbers, pointers and enums",
                        "cannot cast '%s' to '%s'", from_buf, to_buf);
    }
    set_value(out, target);
}

/* ------------------------------------------------------------------ */
/* Struct literals                                                    */
/* ------------------------------------------------------------------ */

static void check_array_literal(Sema *sema, AstArrayLiteral *literal, Type *expected,
                                ExprValue *out) {
    Type *elem_expected = NULL;
    int64_t expected_len = -1;
    if (expected && expected->kind == TY_ARRAY) {
        elem_expected = expected->as.array.elem;
        expected_len = expected->as.array.length;
    }
    if (literal->is_repeat) {
        ExprValue value;
        check_expr_internal(sema, literal->elements[0], elem_expected, &value);
        if (elem_expected) {
            sema_coerce(sema, literal->elements[0], &value, elem_expected, "array element");
        } else {
            if (type_is_untyped(value.type)) {
                sema_default_expr_type(sema, literal->elements[0], &value);
            }
            elem_expected = value.type;
        }
        if (elem_expected->kind == TY_VOID || elem_expected->kind == TY_NULL) {
            sema_error(sema, literal->base.pos, 1, E_TYPE_MISMATCH,
                       "cannot infer an array element type from this value; annotate the type");
            set_value(out, prim(sema, TY_ERROR));
            return;
        }
        if (expected_len >= 0 && expected_len != literal->repeat_count) {
            sema_error(sema, literal->base.pos, 1, E_TYPE_MISMATCH,
                       "array literal repeats %lld times but the type expects %lld elements",
                       (long long)literal->repeat_count, (long long)expected_len);
        }
        set_value(out, type_array(sema->arena, elem_expected, literal->repeat_count));
        return;
    }
    if (literal->count == 0) {
        if (expected && expected->kind == TY_ARRAY) {
            set_value(out, expected);
            return;
        }
        sema_error_note(sema, literal->base.pos, 2, E_TYPE_MISMATCH,
                        "write the type explicitly, e.g. 'let a: [i32; 0]' is not allowed; "
                        "use a non-empty literal",
                        "cannot infer the element type of an empty array literal");
        set_value(out, prim(sema, TY_ERROR));
        return;
    }

    ExprValue *values = ARENA_ARRAY(sema->arena, ExprValue, literal->count);
    for (size_t i = 0; i < literal->count; i++) {
        check_expr_internal(sema, literal->elements[i], elem_expected, &values[i]);
    }
    if (!elem_expected) {
        for (size_t i = 0; i < literal->count; i++) {
            if (!type_is_untyped(values[i].type) && !type_is_error(values[i].type)) {
                elem_expected = values[i].type;
                break;
            }
        }
        if (!elem_expected) {
            bool has_float = false;
            for (size_t i = 0; i < literal->count; i++) {
                if (values[i].type->kind == TY_UNTYPED_FLOAT) {
                    has_float = true;
                }
            }
            elem_expected = has_float ? prim(sema, TY_F64) : prim(sema, TY_I32);
        }
    }
    if (elem_expected->kind == TY_VOID || elem_expected->kind == TY_NULL) {
        sema_error(sema, literal->base.pos, 1, E_TYPE_MISMATCH,
                   "array elements must have a concrete type; annotate the array type");
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    for (size_t i = 0; i < literal->count; i++) {
        if (type_is_error(values[i].type)) {
            continue;
        }
        sema_coerce(sema, literal->elements[i], &values[i], elem_expected, "array element");
    }
    if (expected_len >= 0 && expected_len != (int64_t)literal->count) {
        sema_error(sema, literal->base.pos, 1, E_TYPE_MISMATCH,
                   "array literal has %zu elements but the type expects %lld", literal->count,
                   (long long)expected_len);
    }
    set_value(out, type_array(sema->arena, elem_expected, (int64_t)literal->count));
}

static void check_struct_literal(Sema *sema, AstStructLiteral *literal, ExprValue *out) {
    Type *type = sema_resolve_type(sema, literal->type_syntax);
    if (type_is_error(type)) {
        set_value(out, type);
        return;
    }
    if (type->kind != TY_STRUCT) {
        char type_buf[64];
        expr_type_string(type, type_buf, sizeof(type_buf));
        sema_error(sema, literal->base.pos, 1, E_NOT_A_STRUCT,
                   "'%s' is not a struct type", type_buf);
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    StructInfo *info = type->as.strukt;
    literal->info = info;
    bool *seen = ARENA_ARRAY(sema->arena, bool, info->nfields);
    for (size_t i = 0; i < literal->count; i++) {
        const char *name = literal->field_names[i];
        int found = -1;
        for (size_t f = 0; f < info->nfields; f++) {
            if (strcmp(info->fields[f].name, name) == 0) {
                found = (int)f;
                break;
            }
        }
        if (found < 0) {
            sema_error(sema, literal->field_values[i]->pos, (int)strlen(name), E_UNKNOWN_FIELD,
                       "struct '%s' has no field '%s'", info->name, name);
            continue;
        }
        if (seen[found]) {
            sema_error(sema, literal->field_values[i]->pos, (int)strlen(name), E_DUPLICATE_FIELD,
                       "field '%s' is initialized more than once", name);
            continue;
        }
        seen[found] = true;
        ExprValue value;
        check_expr_internal(sema, literal->field_values[i], info->fields[found].type, &value);
        sema_coerce(sema, literal->field_values[i], &value, info->fields[found].type,
                    "struct field initializer");
    }
    for (size_t f = 0; f < info->nfields; f++) {
        if (!seen[f]) {
            sema_error_note(sema, literal->base.pos, 1, E_UNKNOWN_FIELD,
                            "every field must be initialized in a struct literal",
                            "missing field '%s' in initializer of '%s'", info->fields[f].name,
                            info->name);
        }
    }
    set_value(out, type);
}

/* ------------------------------------------------------------------ */
/* Calls                                                              */
/* ------------------------------------------------------------------ */

/* Reports an argument count mismatch with the real signature in the note. */
static void report_arg_count(Sema *sema, AstCall *call, FunctionInfo *fn) {
    char signature[256];
    fmt_function_signature(fn, signature, sizeof(signature));
    char note[320];
    snprintf(note, sizeof(note), "function signature is: %s", signature);
    sema_error_note(sema, call->base.pos, 1, E_ARG_COUNT,
                    note, "function '%s' expects %zu argument%s but %zu %s given", fn->name,
                    fn->nparams, fn->nparams == 1 ? "" : "s", call->argc,
                    call->argc == 1 ? "was" : "were");
}

static void report_builtin_arg_count(Sema *sema, AstCall *call, const BuiltinFn *fn,
                                     const char *display_name) {
    char signature[256];
    fmt_builtin_signature(fn, signature, sizeof(signature));
    char note[320];
    snprintf(note, sizeof(note), "signature is: %s", signature);
    sema_error_note(sema, call->base.pos, 1, E_ARG_COUNT, note,
                    "'%s' expects %zu argument%s but %zu %s given", display_name, fn->nparams,
                    fn->nparams == 1 ? "" : "s", call->argc, call->argc == 1 ? "was" : "were");
}

static void check_builtin_call(Sema *sema, AstCall *call, const BuiltinFn *candidates[],
                               size_t count, const char *display_name) {
    ExprValue *arg_values = ARENA_ARRAY(sema->arena, ExprValue, call->argc ? call->argc : 1);
    Type **arg_types = ARENA_ARRAY(sema->arena, Type *, call->argc ? call->argc : 1);
    for (size_t i = 0; i < call->argc; i++) {
        check_expr_internal(sema, call->args[i], NULL, &arg_values[i]);
        arg_types[i] = arg_values[i].type;
        if (arg_values[i].type->kind == TY_VOID) {
            sema_error(sema, call->args[i]->pos, 1, E_VOID_VALUE,
                       "a void expression cannot be passed as an argument");
            arg_types[i] = prim(sema, TY_ERROR);
        }
    }

    int best_score = -1;
    const BuiltinFn *best = NULL;
    bool arity_match = false;
    for (size_t c = 0; c < count; c++) {
        const BuiltinFn *candidate = candidates[c];
        if (candidate->nparams != call->argc) {
            continue;
        }
        arity_match = true;
        int score = 0;
        bool ok = true;
        for (size_t i = 0; i < call->argc; i++) {
            int arg_score =
                sema_builtin_arg_score(sema, candidate->params[i], call->args[i], arg_types[i]);
            if (arg_score < 0) {
                ok = false;
                break;
            }
            score += arg_score;
        }
        if (ok && score > best_score) {
            best_score = score;
            best = candidate;
        }
    }
    if (!best) {
        const BuiltinFn *first = candidates[0];
        if (!arity_match) {
            report_builtin_arg_count(sema, call, first, display_name);
        } else {
            StrBuf sb;
            sb_init(&sb);
            for (size_t i = 0; i < call->argc; i++) {
                if (i) {
                    sb_append(&sb, ", ");
                }
                char type_buf[64];
                expr_type_string(arg_types[i], type_buf, sizeof(type_buf));
                sb_append(&sb, type_buf);
            }
            char note[512];
            snprintf(note, sizeof(note),
                     "no overload of '%s' accepts (%s); see docs/language-spec.md",
                     display_name, sb.data ? sb.data : "");
            sb_free(&sb);
            sema_error(sema, call->base.pos, 1, E_ARG_TYPE, "%s", note);
        }
        call->base.type = prim(sema, TY_ERROR);
        return;
    }

    for (size_t i = 0; i < call->argc; i++) {
        Type *target = stdlib_spec_type(&sema->types, best->params[i]);
        if (!target) {
            continue;
        }
        if (arg_values[i].type->kind == TY_ERROR) {
            continue;
        }
        if (best->params[i] == BSPEC_ANY_PTR) {
            if (arg_values[i].type->kind == TY_PTR || arg_values[i].type->kind == TY_NULL ||
                type_is_error(arg_values[i].type)) {
                continue;
            }
            sema_coerce(sema, call->args[i], &arg_values[i], target, "argument");
            continue;
        }
        sema_coerce(sema, call->args[i], &arg_values[i], target, "argument");
    }
    call->builtin = best;
    call->base.type = stdlib_spec_type(&sema->types, best->ret);
}

static void check_call(Sema *sema, AstCall *call, Type *expected, ExprValue *out) {
    RYN_UNUSED(expected);
    FunctionInfo *direct = NULL;
    const BuiltinFn *candidates[32];
    size_t candidate_count = 0;
    const char *display_name = NULL;

    if (call->callee->kind == AST_IDENT) {
        AstIdent *callee = (AstIdent *)call->callee;
        SemaSymbol *symbol = sema_scope_lookup(sema, callee->name);
        if (symbol && symbol->kind == SYM_MODULE) {
            sema_error_note(sema, call->base.pos, (int)strlen(callee->name), E_NOT_CALLABLE,
                            "call one of its functions, e.g. '%s.println(...)'",
                            "module '%s' is not callable", callee->name);
            set_value(out, prim(sema, TY_ERROR));
            return;
        }
        if (symbol) {
            symbol->read = true;
            callee->symbol = symbol;
            if (symbol->type->kind != TY_FN) {
                char type_buf[64];
                expr_type_string(symbol->type, type_buf, sizeof(type_buf));
                sema_error(sema, call->base.pos, 1, E_NOT_CALLABLE,
                           "'%s' has type '%s' and cannot be called", callee->name, type_buf);
                set_value(out, prim(sema, TY_ERROR));
                return;
            }
            call->indirect = true;
            Type *fn_type = symbol->type;
            if (call->argc != fn_type->as.fn.nparams) {
                char note[256];
                snprintf(note, sizeof(note), "this function value expects %zu argument%s",
                         fn_type->as.fn.nparams, fn_type->as.fn.nparams == 1 ? "" : "s");
                sema_error_note(sema, call->base.pos, 1, E_ARG_COUNT, note,
                                "called with %zu argument%s", call->argc,
                                call->argc == 1 ? "" : "s");
                set_value(out, fn_type->as.fn.ret);
                return;
            }
            for (size_t i = 0; i < call->argc; i++) {
                ExprValue value;
                check_expr_internal(sema, call->args[i], fn_type->as.fn.params[i], &value);
                sema_coerce(sema, call->args[i], &value, fn_type->as.fn.params[i], "argument");
            }
            set_value(out, fn_type->as.fn.ret);
            return;
        }
        direct = sema_lookup_function(sema, callee->name);
        if (direct) {
            callee->refers_to_function = true;
            callee->function = direct;
            call->function = direct;
        } else {
            candidate_count = stdlib_candidates(NULL, callee->name, candidates, 32);
            if (candidate_count == 0) {
                sema_error_note(sema, call->base.pos, (int)strlen(callee->name),
                                E_UNDEFINED_FUNCTION,
                                "functions must be declared before use; check the name",
                                "undefined function '%s'", callee->name);
                set_value(out, prim(sema, TY_ERROR));
                return;
            }
            display_name = callee->name;
        }
    } else if (call->callee->kind == AST_FIELD) {
        AstField *field = (AstField *)call->callee;
        const BuiltinModule *module = NULL;
        if (field->object->kind == AST_IDENT) {
            SemaSymbol *object_symbol =
                sema_scope_lookup(sema, ((AstIdent *)field->object)->name);
            if (object_symbol && object_symbol->kind == SYM_MODULE) {
                module = object_symbol->module;
            }
        }
        if (module) {
            candidate_count = stdlib_candidates(module, field->name, candidates, 32);
            if (candidate_count == 0) {
                for (size_t i = 0; i < module->nconstants; i++) {
                    if (strcmp(module->constants[i].name, field->name) == 0) {
                        sema_error(sema, call->base.pos, 1, E_NOT_CALLABLE,
                                   "constant '%s.%s' cannot be called", module->name,
                                   field->name);
                        set_value(out, prim(sema, TY_ERROR));
                        return;
                    }
                }
                char note[256];
                module_member_note(module, note, sizeof(note));
                sema_error_note(sema, call->base.pos, (int)strlen(field->name),
                                E_UNDEFINED_FUNCTION, note,
                                "module '%s' has no function '%s'", module->name, field->name);
                set_value(out, prim(sema, TY_ERROR));
                return;
            }
            display_name = field->name;
        }
    }

    if (!direct && candidate_count == 0) {
        /* Indirect call through a field or another expression. */
        ExprValue callee_value;
        check_expr_internal(sema, call->callee, NULL, &callee_value);
        if (type_is_error(callee_value.type)) {
            set_value(out, callee_value.type);
            return;
        }
        if (callee_value.type->kind != TY_FN) {
            char type_buf[64];
            expr_type_string(callee_value.type, type_buf, sizeof(type_buf));
            sema_error(sema, call->base.pos, 1, E_NOT_CALLABLE,
                       "expression of type '%s' is not callable", type_buf);
            set_value(out, prim(sema, TY_ERROR));
            return;
        }
        call->indirect = true;
        Type *fn_type = callee_value.type;
        if (call->argc != fn_type->as.fn.nparams) {
            sema_error(sema, call->base.pos, 1, E_ARG_COUNT,
                       "function value expects %zu arguments but %zu were given",
                       fn_type->as.fn.nparams, call->argc);
            set_value(out, fn_type->as.fn.ret);
            return;
        }
        for (size_t i = 0; i < call->argc; i++) {
            ExprValue value;
            check_expr_internal(sema, call->args[i], fn_type->as.fn.params[i], &value);
            sema_coerce(sema, call->args[i], &value, fn_type->as.fn.params[i], "argument");
        }
        set_value(out, fn_type->as.fn.ret);
        return;
    }

    if (direct) {
        if (call->argc != direct->nparams) {
            report_arg_count(sema, call, direct);
            set_value(out, direct->return_type);
            return;
        }
        for (size_t i = 0; i < call->argc; i++) {
            ExprValue value;
            check_expr_internal(sema, call->args[i], direct->param_types[i], &value);
            sema_coerce(sema, call->args[i], &value, direct->param_types[i], "argument");
        }
        set_value(out, direct->return_type);
        return;
    }

    check_builtin_call(sema, call, candidates, candidate_count, display_name);
    set_value(out, call->base.type);
}

/* ------------------------------------------------------------------ */
/* Main dispatcher                                                    */
/* ------------------------------------------------------------------ */

static void check_expr_internal(Sema *sema, AstExpr *expr, Type *expected, ExprValue *out) {
    if (!expr) {
        set_value(out, prim(sema, TY_ERROR));
        return;
    }
    switch (expr->kind) {
    case AST_INT_LITERAL:
        check_int_literal(sema, (AstIntLiteral *)expr, expected, out);
        break;
    case AST_FLOAT_LITERAL:
        check_float_literal(sema, (AstFloatLiteral *)expr, expected, out);
        break;
    case AST_STRING_LITERAL:
        set_value(out, prim(sema, TY_STR));
        break;
    case AST_CHAR_LITERAL:
        set_value(out, prim(sema, TY_CHAR));
        break;
    case AST_BOOL_LITERAL:
        set_value(out, prim(sema, TY_BOOL));
        break;
    case AST_NULL_LITERAL:
        set_value(out, prim(sema, TY_NULL));
        break;
    case AST_IDENT:
        check_ident(sema, (AstIdent *)expr, out);
        break;
    case AST_UNARY:
        check_unary(sema, (AstUnary *)expr, expected, out);
        break;
    case AST_BINARY:
        check_binary(sema, (AstBinary *)expr, expected, out);
        break;
    case AST_CALL:
        check_call(sema, (AstCall *)expr, expected, out);
        break;
    case AST_FIELD:
        check_field(sema, (AstField *)expr, expected, out);
        break;
    case AST_INDEX:
        check_index(sema, (AstIndex *)expr, out);
        break;
    case AST_CAST:
        check_cast(sema, (AstCast *)expr, out);
        break;
    case AST_STRUCT_LITERAL:
        check_struct_literal(sema, (AstStructLiteral *)expr, out);
        break;
    case AST_ARRAY_LITERAL:
        check_array_literal(sema, (AstArrayLiteral *)expr, expected, out);
        break;
    case AST_ERROR_EXPR:
        set_value(out, prim(sema, TY_ERROR));
        break;
    default:
        sema_error(sema, expr->pos, 1, E_SYNTAX, "unsupported expression");
        set_value(out, prim(sema, TY_ERROR));
        break;
    }
    expr->type = out->type;
}

void sema_check_expr(Sema *sema, AstExpr *expr, Type *expected, ExprValue *out) {
    check_expr_internal(sema, expr, expected, out);
    if (expr) {
        expr->type = out->type;
    }
}

void sema_check_condition(Sema *sema, AstExpr *expr) {
    ExprValue value;
    check_expr_internal(sema, expr, prim(sema, TY_BOOL), &value);
    if (type_is_error(value.type)) {
        return;
    }
    if (value.type->kind != TY_BOOL) {
        char type_buf[64];
        expr_type_string(value.type, type_buf, sizeof(type_buf));
        sema_error_note(sema, expr->pos, 1, E_TYPE_MISMATCH,
                        "conditions do not implicitly convert to bool",
                        "condition must be bool, found '%s'", type_buf);
    }
}
