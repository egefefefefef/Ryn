#include "sema_internal.h"

struct SemaScope {
    SemaScope *parent;
    SemaSymbol *symbols;
    bool function_scope;
};

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void sema_init(Sema *sema, Arena *arena, Diagnostics *diag, SourceManager *sm) {
    memset(sema, 0, sizeof(*sema));
    sema->arena = arena;
    sema->diag = diag;
    sema->sm = sm;
    types_init(&sema->types, arena);
    FunctionInfoVec_init(&sema->functions);
    StructInfoVec_init(&sema->structs);
    EnumInfoVec_init(&sema->enums);
    BuiltinModuleVec_init(&sema->imports);
    sema->scope = sema_scope_push(sema, true);
    sema->global_scope = sema->scope;
}

void sema_free(Sema *sema) {
    FunctionInfoVec_free(&sema->functions);
    StructInfoVec_free(&sema->structs);
    EnumInfoVec_free(&sema->enums);
    BuiltinModuleVec_free(&sema->imports);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */

void sema_error(Sema *sema, SourcePos pos, int span, DiagCode code, const char *fmt, ...) {
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    diag_error_span(sema->diag, pos, span > 0 ? span : 1, code, "%s", message);
    sema->had_error = true;
}

void sema_error_note(Sema *sema, SourcePos pos, int span, DiagCode code, const char *note,
                     const char *fmt, ...) {
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    diag_error_note(sema->diag, pos, span > 0 ? span : 1, code, note, "%s", message);
    sema->had_error = true;
}

void sema_warn(Sema *sema, SourcePos pos, int span, DiagCode code, const char *fmt, ...) {
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    diag_warn_span(sema->diag, pos, span > 0 ? span : 1, code, "%s", message);
}

void sema_warn_note(Sema *sema, SourcePos pos, int span, DiagCode code, const char *note,
                    const char *fmt, ...) {
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    diag_warn_note(sema->diag, pos, span > 0 ? span : 1, code, note, "%s", message);
}

/* ------------------------------------------------------------------ */
/* Scopes                                                             */
/* ------------------------------------------------------------------ */

SemaScope *sema_scope_push(Sema *sema, bool function_scope) {
    SemaScope *scope = ARENA_ARRAY(sema->arena, SemaScope, 1);
    scope->parent = sema->scope;
    scope->symbols = NULL;
    scope->function_scope = function_scope;
    sema->scope = scope;
    return scope;
}

void sema_scope_pop(Sema *sema) {
    if (sema->scope && sema->scope->parent) {
        sema->scope = sema->scope->parent;
    }
}

SemaSymbol *sema_scope_lookup(Sema *sema, const char *name) {
    for (SemaScope *scope = sema->scope; scope; scope = scope->parent) {
        for (SemaSymbol *symbol = scope->symbols; symbol; symbol = symbol->next_in_scope) {
            if (strcmp(symbol->name, name) == 0) {
                return symbol;
            }
        }
    }
    return NULL;
}

SemaSymbol *sema_scope_lookup_current(Sema *sema, const char *name) {
    for (SemaSymbol *symbol = sema->scope->symbols; symbol; symbol = symbol->next_in_scope) {
        if (strcmp(symbol->name, name) == 0) {
            return symbol;
        }
    }
    return NULL;
}

static SemaSymbol *define_symbol(Sema *sema, const char *name, Type *type, bool is_mutable,
                                 SourcePos pos, bool is_param, bool is_loop_var,
                                 SemaSymbolKind kind) {
    SemaSymbol *existing = sema_scope_lookup_current(sema, name);
    if (existing) {
        sema_error_note(sema, pos, (int)strlen(name), E_DUPLICATE,
                        "each name may only be declared once per scope", "redefinition of '%s'",
                        name);
    } else {
        for (SemaScope *scope = sema->scope->parent; scope; scope = scope->parent) {
            for (SemaSymbol *symbol = scope->symbols; symbol; symbol = symbol->next_in_scope) {
                if (strcmp(symbol->name, name) == 0) {
                    sema_warn_note(sema, pos, (int)strlen(name), W_SHADOWED,
                                   "the outer declaration is hidden inside this scope",
                                   "'%s' shadows an earlier declaration on line %d", name,
                                   symbol->pos.line);
                    goto done_shadow;
                }
            }
        }
    }
done_shadow : {
    SemaSymbol *symbol = ARENA_ARRAY(sema->arena, SemaSymbol, 1);
    symbol->name = name;
    symbol->kind = kind;
    symbol->type = type;
    symbol->is_mutable = is_mutable;
    symbol->initialized = false;
    symbol->read = false;
    symbol->is_param = is_param;
    symbol->is_loop_var = is_loop_var;
    symbol->pos = pos;
    symbol->scope = sema->scope;
    symbol->next_in_scope = sema->scope->symbols;
    sema->scope->symbols = symbol;
    symbol->next_local = sema->locals_head;
    sema->locals_head = symbol;
    return symbol;
}
}

SemaSymbol *sema_scope_define(Sema *sema, const char *name, Type *type, bool is_mutable,
                              SourcePos pos, bool is_param) {
    return define_symbol(sema, name, type, is_mutable, pos, is_param, false, SYM_VARIABLE);
}

void sema_scope_add_module(Sema *sema, const BuiltinModule *module, SourcePos pos) {
    SemaSymbol *existing = sema_scope_lookup_current(sema, module->name);
    if (existing) {
        sema_error(sema, pos, (int)strlen(module->name), E_DUPLICATE,
                   "module '%s' is already imported", module->name);
        return;
    }
    SemaSymbol *symbol = ARENA_ARRAY(sema->arena, SemaSymbol, 1);
    symbol->name = module->name;
    symbol->kind = SYM_MODULE;
    symbol->type = NULL;
    symbol->is_mutable = false;
    symbol->initialized = true;
    symbol->pos = pos;
    symbol->module = module;
    symbol->scope = sema->scope;
    symbol->next_in_scope = sema->scope->symbols;
    sema->scope->symbols = symbol;
}

/* ------------------------------------------------------------------ */
/* Lookups                                                            */
/* ------------------------------------------------------------------ */

FunctionInfo *sema_lookup_function(Sema *sema, const char *name) {
    for (size_t i = 0; i < sema->functions.len; i++) {
        if (strcmp(sema->functions.items[i]->name, name) == 0) {
            return sema->functions.items[i];
        }
    }
    return NULL;
}

StructInfo *sema_lookup_struct(Sema *sema, const char *name) {
    for (size_t i = 0; i < sema->structs.len; i++) {
        if (strcmp(sema->structs.items[i]->name, name) == 0) {
            return sema->structs.items[i];
        }
    }
    return NULL;
}

EnumInfo *sema_lookup_enum(Sema *sema, const char *name) {
    for (size_t i = 0; i < sema->enums.len; i++) {
        if (strcmp(sema->enums.items[i]->name, name) == 0) {
            return sema->enums.items[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Type resolution                                                    */
/* ------------------------------------------------------------------ */

static TypeKind primitive_kind(const char *name) {
    static const struct {
        const char *name;
        TypeKind kind;
    } TABLE[] = {
        {"void", TY_VOID}, {"bool", TY_BOOL}, {"char", TY_CHAR}, {"str", TY_STR},
        {"i8", TY_I8},     {"i16", TY_I16},   {"i32", TY_I32},   {"i64", TY_I64},
        {"u8", TY_U8},     {"u16", TY_U16},   {"u32", TY_U32},   {"u64", TY_U64},
        {"f32", TY_F32},   {"f64", TY_F64},
    };
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) {
        if (strcmp(TABLE[i].name, name) == 0) {
            return TABLE[i].kind;
        }
    }
    return TY_ERROR;
}

Type *sema_resolve_type(Sema *sema, AstType *type) {
    if (!type) {
        return type_prim(&sema->types, TY_VOID);
    }
    if (type->resolved && !type_is_error(type->resolved)) {
        return type->resolved;
    }
    Type *result = type_prim(&sema->types, TY_ERROR);
    switch (type->kind) {
    case AST_TYPE_NAME: {
        if (strcmp(type->name, "<error>") == 0) {
            result = type_prim(&sema->types, TY_ERROR);
            break;
        }
        TypeKind kind = primitive_kind(type->name);
        if (kind != TY_ERROR) {
            result = type_prim(&sema->types, kind);
            break;
        }
        StructInfo *st = sema_lookup_struct(sema, type->name);
        if (st) {
            result = st->type;
            break;
        }
        EnumInfo *en = sema_lookup_enum(sema, type->name);
        if (en) {
            result = en->type;
            break;
        }
        sema_error_note(sema, type->pos, (int)strlen(type->name), E_UNKNOWN_TYPE,
                        "types must be declared before use and names are case sensitive",
                        "unknown type '%s'", type->name);
        break;
    }
    case AST_TYPE_PTR: {
        Type *elem = sema_resolve_type(sema, type->elem);
        result = type_ptr(sema->arena, elem);
        break;
    }
    case AST_TYPE_ARRAY: {
        Type *elem = sema_resolve_type(sema, type->elem);
        if (elem->kind == TY_VOID) {
            sema_error(sema, type->pos, 1, E_TYPE_MISMATCH,
                       "arrays of void are not allowed");
            result = type_prim(&sema->types, TY_ERROR);
            break;
        }
        if (type->array_length <= 0) {
            sema_error(sema, type->pos, 1, E_LITERAL_RANGE,
                       "array length must be at least 1");
            result = type_prim(&sema->types, TY_ERROR);
            break;
        }
        result = type_array(sema->arena, elem, (int64_t)type->array_length);
        break;
    }
    case AST_TYPE_FN: {
        Type **params = ARENA_ARRAY(sema->arena, Type *, type->nparams);
        for (size_t i = 0; i < type->nparams; i++) {
            params[i] = sema_resolve_type(sema, type->params[i]);
            if (params[i]->kind == TY_VOID) {
                sema_error(sema, type->pos, 1, E_TYPE_MISMATCH,
                           "function type parameters cannot be void");
            }
        }
        Type *ret = sema_resolve_type(sema, type->ret);
        result = type_fn(sema->arena, params, type->nparams, ret);
        break;
    }
    default:
        sema_error(sema, type->pos, 1, E_SYNTAX, "invalid type syntax");
        break;
    }
    type->resolved = result;
    return result;
}

bool sema_int_literal_fits(Type *target, int64_t value, uint64_t uvalue) {
    if (!target || target->kind == TY_ERROR) {
        return true;
    }
    switch (target->kind) {
    case TY_I8: return value >= -128 && value <= 127;
    case TY_I16: return value >= -32768 && value <= 32767;
    case TY_I32: return value >= -2147483648LL && value <= 2147483647LL;
    case TY_I64: return uvalue <= (uint64_t)INT64_MAX;
    case TY_U8: return uvalue <= 255;
    case TY_U16: return uvalue <= 65535;
    case TY_U32: return uvalue <= 4294967295ULL;
    case TY_U64: return true;
    case TY_BOOL: return uvalue <= 1;
    case TY_F32:
    case TY_F64: return true;
    default: return false;
    }
}

void sema_materialize_default(Sema *sema, AstExpr *expr, Type *chosen) {
    if (!expr) {
        return;
    }
    if (!type_is_untyped(expr->type)) {
        return;
    }
    expr->type = chosen;
    switch (expr->kind) {
    case AST_UNARY:
        sema_materialize_default(sema, ((AstUnary *)expr)->operand, chosen);
        break;
    case AST_BINARY:
        sema_materialize_default(sema, ((AstBinary *)expr)->lhs, chosen);
        sema_materialize_default(sema, ((AstBinary *)expr)->rhs, chosen);
        break;
    default:
        break;
    }
}

void sema_default_expr_type(Sema *sema, AstExpr *expr, ExprValue *value) {
    if (!value || !type_is_untyped(value->type)) {
        return;
    }
    Type *chosen = value->type->kind == TY_UNTYPED_FLOAT ? type_prim(&sema->types, TY_F64)
                                                         : type_prim(&sema->types, TY_I32);
    sema_materialize_default(sema, expr, chosen);
    value->type = chosen;
    expr->type = chosen;
}

bool sema_coerce(Sema *sema, AstExpr *expr, ExprValue *value, Type *target, const char *context) {
    char expected[128];
    char actual[128];
    if (!value || !value->type || type_is_error(value->type) || type_is_error(target)) {
        return true;
    }
    if (type_eq(value->type, target)) {
        return true;
    }
    if (value->type->kind == TY_VOID) {
        sema_error_note(sema, expr->pos, 1, E_VOID_VALUE,
                        "a function returning void does not produce a value",
                        "void value used in %s", context);
        return false;
    }
    if (value->type->kind == TY_UNTYPED_INT) {
        if (sema_int_literal_fits(target, ((AstIntLiteral *)expr)->value,
                                  ((AstIntLiteral *)expr)->uvalue) ||
            expr->kind != AST_INT_LITERAL) {
            if (type_is_integer(target) || type_is_float(target)) {
                sema_materialize_default(sema, expr, target);
                value->type = target;
                expr->type = target;
                return true;
            }
        } else {
            type_to_string_buf(target, expected, sizeof(expected));
            sema_error(sema, expr->pos, 1, E_LITERAL_RANGE,
                       "integer literal does not fit in '%s'", expected);
            return false;
        }
    }
    if (value->type->kind == TY_UNTYPED_FLOAT) {
        if (type_is_float(target)) {
            sema_materialize_default(sema, expr, target);
            value->type = target;
            expr->type = target;
            return true;
        }
        if (type_is_integer(target)) {
            type_to_string_buf(target, expected, sizeof(expected));
            sema_error_note(sema, expr->pos, 1, E_TYPE_MISMATCH,
                            "floating point literals do not implicitly convert to integers; use 'as'",
                            "cannot use a float literal as '%s' in %s", expected, context);
            return false;
        }
    }
    if (value->type->kind == TY_NULL) {
        if (target->kind == TY_PTR) {
            return true;
        }
        type_to_string_buf(target, expected, sizeof(expected));
        sema_error_note(sema, expr->pos, 1, E_TYPE_MISMATCH,
                        "null can only be used with pointer types",
                        "null cannot initialize '%s' in %s", expected, context);
        return false;
    }
    if (type_can_widen(value->type, target)) {
        return true;
    }
    type_to_string_buf(value->type, actual, sizeof(actual));
    type_to_string_buf(target, expected, sizeof(expected));
    sema_error_note(sema, expr->pos, 1, E_TYPE_MISMATCH,
                    "explicit conversion is required ('value as T')",
                    "type mismatch in %s: expected '%s', found '%s'", context, expected, actual);
    return false;
}

/* ------------------------------------------------------------------ */
/* Registration passes                                                */
/* ------------------------------------------------------------------ */

static void register_imports(Sema *sema) {
    AstProgram *program = sema->program;
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind != AST_IMPORT_DECL) {
            continue;
        }
        AstImportDecl *import = (AstImportDecl *)decl;
        const BuiltinModule *module = stdlib_find_module(import->module);
        if (!module) {
            char note[256];
            snprintf(note, sizeof(note), "available modules are: %s",
                     stdlib_module_names_csv());
            sema_error_note(sema, import->base.pos, (int)strlen(import->module), E_UNKNOWN_MODULE,
                            note, "unknown module '%s'", import->module);
            continue;
        }
        bool already = false;
        for (size_t k = 0; k < sema->imports.len; k++) {
            if (sema->imports.items[k] == module) {
                already = true;
                break;
            }
        }
        if (!already) {
            BuiltinModuleVec_push(&sema->imports, module);
        }
        sema_scope_add_module(sema, module, import->base.pos);
    }
}

static void register_types(Sema *sema) {
    AstProgram *program = sema->program;
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind == AST_STRUCT_DECL) {
            AstStructDecl *st = (AstStructDecl *)decl;
            if (sema_lookup_struct(sema, st->name) || sema_lookup_enum(sema, st->name)) {
                sema_error_note(sema, st->base.pos, (int)strlen(st->name), E_DUPLICATE,
                                "type names share one namespace", "type '%s' is already declared",
                                st->name);
                continue;
            }
            StructInfo *info = ARENA_ARRAY(sema->arena, StructInfo, 1);
            info->name = st->name;
            info->fields = ARENA_ARRAY(sema->arena, StructField, st->nfields);
            info->nfields = st->nfields;
            info->resolved = false;
            info->resolving = false;
            info->decl_pos = st->base.pos;
            info->ast_decl = st;
            for (size_t f = 0; f < st->nfields; f++) {
                info->fields[f].name = st->fields[f].name;
                info->fields[f].pos = st->fields[f].pos;
                info->fields[f].type = NULL;
            }
            st->info = info;
            type_new_struct(&sema->types, info);
            StructInfoVec_push(&sema->structs, info);
        } else if (decl->kind == AST_ENUM_DECL) {
            AstEnumDecl *en = (AstEnumDecl *)decl;
            if (sema_lookup_struct(sema, en->name) || sema_lookup_enum(sema, en->name)) {
                sema_error_note(sema, en->base.pos, (int)strlen(en->name), E_DUPLICATE,
                                "type names share one namespace", "type '%s' is already declared",
                                en->name);
                continue;
            }
            EnumInfo *info = ARENA_ARRAY(sema->arena, EnumInfo, 1);
            info->name = en->name;
            info->variants = ARENA_ARRAY(sema->arena, EnumVariant, en->nvariants);
            info->nvariants = en->nvariants;
            info->decl_pos = en->base.pos;
            info->is_wide = false;
            for (size_t v = 0; v < en->nvariants; v++) {
                info->variants[v].name = en->variants[v].name;
                info->variants[v].value = en->variants[v].value;
                info->variants[v].pos = en->variants[v].pos;
                if (en->variants[v].value < INT32_MIN || en->variants[v].value > INT32_MAX) {
                    info->is_wide = true;
                }
                for (size_t k = 0; k < v; k++) {
                    if (strcmp(en->variants[k].name, en->variants[v].name) == 0) {
                        sema_error(sema, en->variants[v].pos,
                                   (int)strlen(en->variants[v].name), E_DUPLICATE,
                                   "duplicate variant '%s' in enum '%s'", en->variants[v].name,
                                   en->name);
                    }
                }
            }
            en->info = info;
            type_new_enum(&sema->types, info);
            EnumInfoVec_push(&sema->enums, info);
        }
    }
}

static void resolve_struct_fields(Sema *sema, StructInfo *info);

static void ensure_struct_complete(Sema *sema, StructInfo *info, SourcePos use_pos) {
    if (info->resolved) {
        return;
    }
    if (info->resolving) {
        char note[256];
        snprintf(note, sizeof(note),
                 "a struct cannot contain itself by value; use 'ptr<%s>' instead",
                 info->name);
        sema_error_note(sema, use_pos, (int)strlen(info->name), E_TYPE_MISMATCH, note,
                        "recursive struct '%s' has infinite size", info->name);
        return;
    }
    resolve_struct_fields(sema, info);
}

static void ensure_type_complete(Sema *sema, Type *type, SourcePos use_pos) {
    if (!type) {
        return;
    }
    if (type->kind == TY_STRUCT) {
        ensure_struct_complete(sema, type->as.strukt, use_pos);
    } else if (type->kind == TY_ARRAY) {
        ensure_type_complete(sema, type->as.array.elem, use_pos);
    }
}

static void resolve_struct_fields(Sema *sema, StructInfo *info) {
    if (info->resolved || info->resolving) {
        return;
    }
    info->resolving = true;
    for (size_t i = 0; i < info->nfields; i++) {
        StructField *field = &info->fields[i];
        for (size_t k = 0; k < i; k++) {
            if (strcmp(info->fields[k].name, field->name) == 0) {
                sema_error(sema, field->pos, (int)strlen(field->name), E_DUPLICATE_FIELD,
                           "duplicate field '%s' in struct '%s'", field->name, info->name);
            }
        }
        AstStructDecl *ast_struct = (AstStructDecl *)info->ast_decl;
        field->type = sema_resolve_type(sema, ast_struct->fields[i].type_syntax);
        if (field->type->kind == TY_VOID) {
            sema_error(sema, field->pos, (int)strlen(field->name), E_TYPE_MISMATCH,
                       "field '%s' cannot have type void", field->name);
            field->type = type_prim(&sema->types, TY_ERROR);
        }
        ensure_type_complete(sema, field->type, field->pos);
    }
    info->resolving = false;
    info->resolved = true;
}

static void register_functions(Sema *sema) {
    AstProgram *program = sema->program;
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind != AST_FN_DECL) {
            continue;
        }
        AstFnDecl *fn_decl = (AstFnDecl *)decl;
        if (sema_lookup_function(sema, fn_decl->name)) {
            sema_error_note(sema, fn_decl->base.pos, (int)strlen(fn_decl->name), E_DUPLICATE,
                            "function overloading is not supported in Ryn 0.2",
                            "function '%s' is already declared", fn_decl->name);
            continue;
        }
        FunctionInfo *fn = ARENA_ARRAY(sema->arena, FunctionInfo, 1);
        fn->name = fn_decl->name;
        if (fn_decl->is_extern) {
            fn->c_name = fn_decl->name;
        } else {
            char *sanitized = ryn_identifier_from(fn_decl->name);
            StrBuf sb;
            sb_init(&sb);
            sb_appendf(&sb, "ryn_%s", sanitized);
            fn->c_name = arena_strdup(sema->arena, sb.data);
            sb_free(&sb);
            free(sanitized);
        }
        fn->nparams = fn_decl->nparams;
        fn->param_types = ARENA_ARRAY(sema->arena, Type *, fn_decl->nparams);
        fn->pos = fn_decl->base.pos;
        fn->is_extern = fn_decl->is_extern;
        fn->decl = fn_decl;
        fn->index = (int)sema->functions.len;
        for (size_t p = 0; p < fn_decl->nparams; p++) {
            AstParam *param = &fn_decl->params[p];
            for (size_t k = 0; k < p; k++) {
                if (strcmp(fn_decl->params[k].name, param->name) == 0) {
                    sema_error(sema, param->pos, (int)strlen(param->name), E_DUPLICATE,
                               "duplicate parameter '%s' in function '%s'", param->name,
                               fn_decl->name);
                }
            }
            Type *type = sema_resolve_type(sema, param->type_syntax);
            if (type->kind == TY_VOID) {
                sema_error(sema, param->pos, (int)strlen(param->name), E_TYPE_MISMATCH,
                           "parameter '%s' cannot have type void", param->name);
                type = type_prim(&sema->types, TY_ERROR);
            }
            if (type->kind == TY_ARRAY) {
                sema_error_note(sema, param->pos, (int)strlen(param->name), E_TYPE_MISMATCH,
                                "arrays decay in C; Ryn requires an explicit pointer",
                                "parameter '%s' cannot be an array; use ptr<...>", param->name);
                type = type_prim(&sema->types, TY_ERROR);
            }
            param->type = type;
            fn->param_types[p] = type;
        }
        Type *ret = fn_decl->return_type_syntax
                        ? sema_resolve_type(sema, fn_decl->return_type_syntax)
                        : type_prim(&sema->types, TY_VOID);
        if (ret->kind == TY_ARRAY) {
            sema_error(sema, fn_decl->base.pos, (int)strlen(fn_decl->name), E_TYPE_MISMATCH,
                       "functions cannot return arrays by value");
            ret = type_prim(&sema->types, TY_ERROR);
        }
        fn->return_type = ret;
        fn->fn_type = type_fn(sema->arena, fn->param_types, fn->nparams, ret);
        fn_decl->info = fn;
        FunctionInfoVec_push(&sema->functions, fn);
    }
}

static void check_main(Sema *sema) {
    FunctionInfo *main_fn = sema_lookup_function(sema, "main");
    SourcePos start = {0, 1, 1, 0};
    if (!main_fn) {
        sema_error_note(sema, start, 4, E_MISSING_MAIN,
                        "a program needs exactly one 'fn main() -> i32'",
                        "missing 'main' function");
        return;
    }
    if (main_fn->is_extern) {
        sema_error(sema, main_fn->pos, 4, E_BAD_MAIN, "'main' cannot be an extern function");
    }
    if (main_fn->nparams != 0) {
        sema_error_note(sema, main_fn->pos, 4, E_BAD_MAIN,
                        "command line arguments are planned for a future release",
                        "'main' must not take parameters in Ryn 0.2");
    }
    if (!(main_fn->return_type->kind == TY_I32 || main_fn->return_type->kind == TY_VOID ||
          type_is_error(main_fn->return_type))) {
        sema_error_note(sema, main_fn->pos, 4, E_BAD_MAIN,
                        "use 'fn main() -> i32' and return an exit code, or 'fn main():' for 0",
                        "'main' must return i32 or void");
    }
}

/* ------------------------------------------------------------------ */
/* Flow analysis                                                      */
/* ------------------------------------------------------------------ */

void sema_flow_capture(Sema *sema, FlowState *state) {
    state->items = NULL;
    state->len = 0;
    state->cap = 0;
    for (SemaScope *scope = sema->scope; scope; scope = scope->parent) {
        for (SemaSymbol *symbol = scope->symbols; symbol; symbol = symbol->next_in_scope) {
            if (symbol->kind != SYM_VARIABLE) {
                continue;
            }
            if (state->len == state->cap) {
                size_t cap = state->cap ? state->cap * 2 : 16;
                FlowEntry *items = (FlowEntry *)realloc(state->items, cap * sizeof(FlowEntry));
                if (!items) {
                    fprintf(stderr, "ryn: out of memory\n");
                    abort();
                }
                state->items = items;
                state->cap = cap;
            }
            state->items[state->len].symbol = symbol;
            state->items[state->len].initialized = symbol->initialized;
            state->len++;
        }
    }
}

void sema_flow_restore(const FlowState *state) {
    for (size_t i = 0; i < state->len; i++) {
        state->items[i].symbol->initialized = state->items[i].initialized;
    }
}

void sema_flow_merge_and(FlowState *into, const FlowState *other) {
    size_t n = into->len < other->len ? into->len : other->len;
    for (size_t i = 0; i < n; i++) {
        if (into->items[i].symbol == other->items[i].symbol) {
            into->items[i].initialized = into->items[i].initialized &&
                                         other->items[i].initialized;
        } else {
            /* Different scope shapes (should not happen); fall back to "maybe". */
            into->items[i].initialized = false;
        }
    }
}

void sema_flow_free(FlowState *state) {
    free(state->items);
    state->items = NULL;
    state->len = 0;
    state->cap = 0;
}

static bool block_exits(const AstBlock *block) {
    if (!block) {
        return false;
    }
    for (size_t i = 0; i < block->count; i++) {
        if (sema_stmt_exits(block->stmts[i])) {
            return true;
        }
    }
    return false;
}

bool sema_stmt_exits(const AstStmt *stmt) {
    if (!stmt) {
        return false;
    }
    switch (stmt->kind) {
    case AST_RETURN:
    case AST_BREAK:
    case AST_CONTINUE:
        return true;
    case AST_IF: {
        const AstIf *if_stmt = (const AstIf *)stmt;
        return if_stmt->else_branch && block_exits(if_stmt->then_block) &&
               sema_stmt_exits(if_stmt->else_branch);
    }
    case AST_BLOCK_STMT:
        return block_exits(((const AstBlockStmt *)stmt)->block);
    default:
        return false;
    }
}

static bool stmt_always_returns(const AstStmt *stmt) {
    if (!stmt) {
        return false;
    }
    switch (stmt->kind) {
    case AST_RETURN:
        return true;
    case AST_IF: {
        const AstIf *if_stmt = (const AstIf *)stmt;
        return if_stmt->else_branch && sema_block_always_returns(if_stmt->then_block) &&
               stmt_always_returns(if_stmt->else_branch);
    }
    case AST_BLOCK_STMT:
        return sema_block_always_returns(((const AstBlockStmt *)stmt)->block);
    default:
        return false;
    }
}

bool sema_block_always_returns(const AstBlock *block) {
    if (!block) {
        return false;
    }
    for (size_t i = 0; i < block->count; i++) {
        if (stmt_always_returns(block->stmts[i])) {
            return true;
        }
    }
    return false;
}

void sema_mark_initialized(AstExpr *target) {
    AstExpr *expr = target;
    while (expr) {
        if (expr->kind == AST_IDENT) {
            AstIdent *ident = (AstIdent *)expr;
            if (ident->symbol) {
                ident->symbol->initialized = true;
            }
            return;
        }
        if (expr->kind == AST_FIELD) {
            expr = ((AstField *)expr)->object;
        } else if (expr->kind == AST_INDEX) {
            expr = ((AstIndex *)expr)->object;
        } else if (expr->kind == AST_UNARY && ((AstUnary *)expr)->op == UN_DEREF) {
            return; /* the pointee is not a tracked local */
        } else {
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

bool sema_analyze(Sema *sema, AstProgram *program) {
    sema->program = program;
    register_imports(sema);
    register_types(sema);
    for (size_t i = 0; i < sema->structs.len; i++) {
        ensure_struct_complete(sema, sema->structs.items[i], sema->structs.items[i]->decl_pos);
    }
    register_functions(sema);
    check_main(sema);

    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind == AST_FN_DECL) {
            AstFnDecl *fn = (AstFnDecl *)decl;
            if (!fn->is_extern && fn->info) {
                sema_check_function(sema, fn);
            }
        }
    }
    return !sema->had_error;
}
