#include "codegen.h"

#include "builtins.h"
#include "sema.h"

#include <inttypes.h>

typedef struct FnTypeEntry {
    Type *type;
    char name[24];
} FnTypeEntry;

typedef struct Codegen {
    Arena *arena;
    Diagnostics *diag;
    AstProgram *program;
    IrModule *module;
    TypeContext *types;
    const CodegenOptions *options;
    bool failed;

    /* Function types are emitted as typedef'd function pointers so that they
     * can be used as parameters, fields and return types. */
    FnTypeEntry *fn_types;
    size_t nfn_types;
    size_t fn_capacity;

    StructInfo **visited_structs;
    size_t nvisited_structs;
    size_t visited_capacity;
} Codegen;

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */

static void codegen_failure(Codegen *cg, SourcePos pos, const char *fmt, ...) {
    if (cg->failed) {
        return;
    }
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    diag_error_note(cg->diag, pos, 1, E_UNSUPPORTED,
                    "this construct is not supported by the C backend yet",
                    "C backend: %s", message);
    cg->failed = true;
}

/* ------------------------------------------------------------------ */
/* C type names                                                       */
/* ------------------------------------------------------------------ */

static const char *fn_type_name(Codegen *cg, Type *type);
static void collect_types(Codegen *cg, Type *type);

static const char *struct_c_name(Codegen *cg, StructInfo *info) {
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "ryn_s_%s", info->name);
    const char *name = arena_strdup(cg->arena, sb.data);
    sb_free(&sb);
    return name;
}

static const char *enum_c_name(Codegen *cg, EnumInfo *info) {
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "ryn_e_%s", info->name);
    const char *name = arena_strdup(cg->arena, sb.data);
    sb_free(&sb);
    return name;
}

/* Base C spelling for scalar-ish types (no declarator). */
static const char *c_base_type(Codegen *cg, Type *type) {
    switch (type->kind) {
    case TY_VOID: return "void";
    case TY_BOOL: return "bool";
    case TY_CHAR: return "uint8_t";
    case TY_STR: return "const char *";
    case TY_I8: return "int8_t";
    case TY_I16: return "int16_t";
    case TY_I32: return "int32_t";
    case TY_I64: return "int64_t";
    case TY_U8: return "uint8_t";
    case TY_U16: return "uint16_t";
    case TY_U32: return "uint32_t";
    case TY_U64: return "uint64_t";
    case TY_F32: return "float";
    case TY_F64: return "double";
    case TY_STRUCT: return struct_c_name(cg, type->as.strukt);
    case TY_ENUM: return enum_c_name(cg, type->as.enumer);
    case TY_FN: return fn_type_name(cg, type);
    case TY_NULL: return "void *";
    default:
        break;
    }
    char buffer[128];
    type_to_string_buf(type, buffer, sizeof(buffer));
    codegen_failure(cg, (SourcePos){0, 1, 1, 0}, "cannot spell type '%s'", buffer);
    return "void";
}

/* Returns a C type usable for temporaries (scalar or pointer). Arrays and
 * function types must go through c_decl. */
static const char *c_type(Codegen *cg, Type *type) {
    if (!type) {
        return "void";
    }
    if (type->kind == TY_PTR) {
        Type *elem = type->as.ptr.elem;
        if (elem->kind == TY_ARRAY) {
            char buffer[128];
            type_to_string_buf(type, buffer, sizeof(buffer));
            codegen_failure(cg, (SourcePos){0, 1, 1, 0},
                            "pointer to %s is not supported", buffer);
            return "void *";
        }
        StrBuf sb;
        sb_init(&sb);
        sb_append(&sb, c_type(cg, elem));
        sb_append(&sb, "*");
        const char *result = arena_strdup(cg->arena, sb.data);
        sb_free(&sb);
        return result;
    }
    return c_base_type(cg, type);
}

/* Full declarator: "<base> <name><suffix>". */
static const char *c_decl(Codegen *cg, Type *type, const char *name) {
    StrBuf sb;
    sb_init(&sb);
    if (!type) {
        sb_appendf(&sb, "void %s", name);
        const char *result = arena_strdup(cg->arena, sb.data);
        sb_free(&sb);
        return result;
    }
    switch (type->kind) {
    case TY_ARRAY: {
        StrBuf dims;
        sb_init(&dims);
        Type *elem = type;
        while (elem->kind == TY_ARRAY) {
            sb_appendf(&dims, "[%lld]", (long long)elem->as.array.length);
            elem = elem->as.array.elem;
        }
        if (elem->kind == TY_PTR) {
            Type *target = elem->as.ptr.elem;
            if (target->kind == TY_ARRAY) {
                codegen_failure(cg, (SourcePos){0, 1, 1, 0},
                                "array of pointers to arrays is not supported");
                sb_appendf(&sb, "void *%s", name);
                break;
            }
            sb_appendf(&sb, "%s %s%s", c_type(cg, elem), name, dims.data);
        } else if (elem->kind == TY_FN) {
            codegen_failure(cg, (SourcePos){0, 1, 1, 0}, "arrays of functions are invalid");
            sb_appendf(&sb, "void *%s", name);
        } else {
            sb_appendf(&sb, "%s %s%s", c_base_type(cg, elem), name, dims.data);
        }
        sb_free(&dims);
        break;
    }
    case TY_FN:
        sb_appendf(&sb, "%s %s", fn_type_name(cg, type), name);
        break;
    case TY_PTR: {
        Type *elem = type->as.ptr.elem;
        if (elem->kind == TY_ARRAY) {
            StrBuf dims;
            sb_init(&dims);
            Type *inner = elem;
            while (inner->kind == TY_ARRAY) {
                sb_appendf(&dims, "[%lld]", (long long)inner->as.array.length);
                inner = inner->as.array.elem;
            }
            sb_appendf(&sb, "%s (*%s)%s", c_base_type(cg, inner), name, dims.data);
            sb_free(&dims);
        } else {
            sb_appendf(&sb, "%s %s", c_type(cg, type), name);
        }
        break;
    }
    default:
        sb_appendf(&sb, "%s %s", c_base_type(cg, type), name);
        break;
    }
    const char *result = arena_strdup(cg->arena, sb.data);
    sb_free(&sb);
    return result;
}

static const char *c_param_type(Codegen *cg, Type *type) {
    StrBuf sb;
    sb_init(&sb);
    if (!type) {
        sb_append(&sb, "void");
    } else if (type->kind == TY_FN || type->kind == TY_ARRAY || type->kind == TY_PTR) {
        sb_append(&sb, c_decl(cg, type, ""));
    } else {
        sb_append(&sb, c_base_type(cg, type));
    }
    /* Trim trailing spaces that come from an empty declarator name. */
    while (sb.len > 0 && sb.data[sb.len - 1] == ' ') {
        sb.data[--sb.len] = '\0';
    }
    const char *result = arena_strdup(cg->arena, sb.data);
    sb_free(&sb);
    return result;
}

/* ------------------------------------------------------------------ */
/* Literals / names                                                   */
/* ------------------------------------------------------------------ */

static void emit_c_string(FILE *out, const char *value, size_t length) {
    fputc('"', out);
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        switch (c) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (c >= 0x20 && c < 0x7F) {
                fputc((int)c, out);
            } else {
                /* Always three octal digits: unambiguous in C. */
                fprintf(out, "\\%03o", (unsigned)c);
            }
            break;
        }
    }
    fputc('"', out);
}

static const char *sanitize_identifier(Arena *arena, const char *name) {
    size_t len = name ? strlen(name) : 0;
    char *out = ARENA_ARRAY(arena, char, len + 1);
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_';
        out[i] = ok ? c : '_';
    }
    out[len] = '\0';
    if (len == 0) {
        out[0] = '_';
        out[1] = '\0';
    }
    return out;
}

static const char *local_c_name(Codegen *cg, const IrFunction *fn, size_t index) {
    const IrLocal *local = &fn->locals[index];
    const char *sanitized = sanitize_identifier(cg->arena, local->name);
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "%c%zu_%s", local->is_param ? 'p' : 'v', index, sanitized);
    const char *result = arena_strdup(cg->arena, sb.data);
    sb_free(&sb);
    return result;
}

static void emit_int_literal(FILE *out, Type *type, int64_t value, uint64_t uvalue) {
    switch (type->kind) {
    case TY_I8:
    case TY_I16:
    case TY_I32:
        fprintf(out, "%" PRId64, value);
        break;
    case TY_I64:
        if (value == INT64_MIN) {
            fputs("(-INT64_C(9223372036854775807) - 1)", out);
        } else {
            fprintf(out, "INT64_C(%" PRId64 ")", value);
        }
        break;
    case TY_U8:
    case TY_U16:
    case TY_U32:
        fprintf(out, "%" PRIu64 "u", uvalue);
        break;
    case TY_U64:
        fprintf(out, "UINT64_C(%" PRIu64 ")", uvalue);
        break;
    case TY_BOOL:
        fputs(uvalue ? "true" : "false", out);
        break;
    case TY_CHAR:
        fprintf(out, "(uint8_t)%" PRIu64, uvalue);
        break;
    case TY_ENUM:
        fprintf(out, "(%" PRId64 ")", value);
        break;
    default:
        fprintf(out, "%" PRId64, value);
        break;
    }
}

static void emit_float_literal(FILE *out, Type *type, double value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.17g", value);
    bool marker = false;
    for (char *p = buffer; *p; p++) {
        if (*p == '.' || *p == 'e' || *p == 'E' || *p == 'n' || *p == 'i') {
            marker = true;
            break;
        }
    }
    if (!marker) {
        size_t len = strlen(buffer);
        if (len + 3 < sizeof(buffer)) {
            memcpy(buffer + len, ".0", 3);
        }
    }
    if (type->kind == TY_F32) {
        fprintf(out, "((float)%s)", buffer);
    } else {
        fprintf(out, "%s", buffer);
    }
}

static const char *temp_name(Codegen *cg, int index) {
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "t%d", index);
    const char *result = arena_strdup(cg->arena, sb.data);
    sb_free(&sb);
    return result;
}

/* ------------------------------------------------------------------ */
/* Function type registry                                             */
/* ------------------------------------------------------------------ */

static const char *fn_type_name(Codegen *cg, Type *type) {
    for (size_t i = 0; i < cg->nfn_types; i++) {
        if (cg->fn_types[i].type == type || type_eq(cg->fn_types[i].type, type)) {
            return cg->fn_types[i].name;
        }
    }
    if (cg->nfn_types == cg->fn_capacity) {
        size_t cap = cg->fn_capacity ? cg->fn_capacity * 2 : 8;
        FnTypeEntry *grown = (FnTypeEntry *)realloc(cg->fn_types, cap * sizeof(FnTypeEntry));
        if (!grown) {
            fprintf(stderr, "ryn: out of memory\n");
            abort();
        }
        cg->fn_types = grown;
        cg->fn_capacity = cap;
    }
    FnTypeEntry *entry = &cg->fn_types[cg->nfn_types];
    entry->type = type;
    snprintf(entry->name, sizeof(entry->name), "ryn_fn_%zu", cg->nfn_types);
    cg->nfn_types++;
    return entry->name;
}

static bool struct_was_visited(Codegen *cg, StructInfo *info) {
    for (size_t i = 0; i < cg->nvisited_structs; i++) {
        if (cg->visited_structs[i] == info) {
            return true;
        }
    }
    return false;
}

static void mark_struct_visited(Codegen *cg, StructInfo *info) {
    if (cg->nvisited_structs == cg->visited_capacity) {
        size_t cap = cg->visited_capacity ? cg->visited_capacity * 2 : 8;
        StructInfo **grown =
            (StructInfo **)realloc(cg->visited_structs, cap * sizeof(StructInfo *));
        if (!grown) {
            fprintf(stderr, "ryn: out of memory\n");
            abort();
        }
        cg->visited_structs = grown;
        cg->visited_capacity = cap;
    }
    cg->visited_structs[cg->nvisited_structs++] = info;
}

static void collect_types(Codegen *cg, Type *type) {
    if (!type) {
        return;
    }
    switch (type->kind) {
    case TY_FN:
        /* Register before recursing so recursive struct/function type graphs
         * terminate; reverse-order emission puts dependencies first. */
        (void)fn_type_name(cg, type);
        collect_types(cg, type->as.fn.ret);
        for (size_t i = 0; i < type->as.fn.nparams; i++) {
            collect_types(cg, type->as.fn.params[i]);
        }
        break;
    case TY_PTR:
        collect_types(cg, type->as.ptr.elem);
        break;
    case TY_ARRAY:
        collect_types(cg, type->as.array.elem);
        break;
    case TY_STRUCT:
        if (struct_was_visited(cg, type->as.strukt)) {
            return;
        }
        mark_struct_visited(cg, type->as.strukt);
        for (size_t i = 0; i < type->as.strukt->nfields; i++) {
            collect_types(cg, type->as.strukt->fields[i].type);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Struct and enum declarations                                       */
/* ------------------------------------------------------------------ */

typedef struct EmitState {
    int *state; /* 0 = new, 1 = visiting, 2 = done */
    size_t count;
} EmitState;

static void emit_struct_definition(Codegen *cg, FILE *out, StructInfo *info, EmitState *state);
static void emit_struct_dependencies(Codegen *cg, FILE *out, Type *type, EmitState *state) {
    if (!type) {
        return;
    }
    if (type->kind == TY_STRUCT) {
        emit_struct_definition(cg, out, type->as.strukt, state);
    } else if (type->kind == TY_ARRAY) {
        emit_struct_dependencies(cg, out, type->as.array.elem, state);
    }
}

static void emit_struct_definition(Codegen *cg, FILE *out, StructInfo *info, EmitState *state) {
    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < cg->program->decls.len; i++) {
        AstDecl *decl = cg->program->decls.items[i];
        if (decl->kind == AST_STRUCT_DECL && ((AstStructDecl *)decl)->info == info) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }
    if (state->state[index] == 2) {
        return;
    }
    if (state->state[index] == 1) {
        codegen_failure(cg, info->decl_pos, "recursive struct '%s' by value", info->name);
        return;
    }
    state->state[index] = 1;
    for (size_t f = 0; f < info->nfields; f++) {
        emit_struct_dependencies(cg, out, info->fields[f].type, state);
    }
    fprintf(out, "struct %s {\n", struct_c_name(cg, info));
    for (size_t f = 0; f < info->nfields; f++) {
        const char *field_name = sanitize_identifier(cg->arena, info->fields[f].name);
        StrBuf sb;
        sb_init(&sb);
        sb_appendf(&sb, "f_%s", field_name);
        fprintf(out, "    %s;\n", c_decl(cg, info->fields[f].type, sb.data));
        sb_free(&sb);
    }
    fputs("};\n\n", out);
    state->state[index] = 2;
}

/* ------------------------------------------------------------------ */
/* Function emission                                                  */
/* ------------------------------------------------------------------ */

static const char *c_binop(BinOp op) {
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
    default: return "?";
    }
}

static const char *unsigned_type_for(Type *type) {
    switch (type->kind) {
    case TY_I8: return "uint8_t";
    case TY_I16: return "uint16_t";
    case TY_I32: return "uint32_t";
    case TY_I64: return "uint64_t";
    case TY_U8: return "uint8_t";
    case TY_U16: return "uint16_t";
    case TY_U32: return "uint32_t";
    case TY_U64: return "uint64_t";
    case TY_CHAR: return "uint8_t";
    case TY_BOOL: return "bool";
    default: return NULL;
    }
}

static int type_bits(Type *type) {
    switch (type->kind) {
    case TY_I8:
    case TY_U8:
    case TY_CHAR:
    case TY_BOOL:
        return 8;
    case TY_I16:
    case TY_U16:
        return 16;
    case TY_I32:
    case TY_U32:
        return 32;
    case TY_I64:
    case TY_U64:
        return 64;
    case TY_F32:
        return 32;
    case TY_F64:
        return 64;
    default:
        return 64;
    }
}

static const char *int_min_macro(Type *type) {
    switch (type->kind) {
    case TY_I8: return "INT8_MIN";
    case TY_I16: return "INT16_MIN";
    case TY_I32: return "INT32_MIN";
    case TY_I64: return "INT64_MIN";
    default: return NULL;
    }
}

static void emit_bin_instr(Codegen *cg, FILE *out, const IrFunction *fn, const IrInstr *instr) {
    Type *type = instr->type;
    const char *dst = temp_name(cg, instr->dst);
    const char *a = temp_name(cg, instr->a);
    const char *b = temp_name(cg, instr->b);
    const char *ctype = c_type(cg, type);
    if (type_is_float(type)) {
        fprintf(out, "    %s = %s %s %s;\n", dst, a, c_binop(instr->binop), b);
        return;
    }
    const char *utype = unsigned_type_for(type);
    int bits = type_bits(type);
    bool is_signed = type_is_signed_int(type);
    if (!utype) {
        codegen_failure(cg, (SourcePos){0, 1, 1, 0}, "arithmetic on unsupported type");
        return;
    }
    switch (instr->binop) {
    case BIN_ADD:
    case BIN_SUB:
    case BIN_MUL:
        fprintf(out, "    %s = (%s)((%s)%s %s (%s)%s);\n", dst, ctype, utype, a,
                c_binop(instr->binop), utype, b);
        break;
    case BIN_DIV:
    case BIN_MOD: {
        fprintf(out, "    if (%s == 0) ryn_panic_div_zero();\n", b);
        if (is_signed) {
            const char *min_macro = int_min_macro(type);
            if (instr->binop == BIN_DIV) {
                fprintf(out, "    %s = (%s == %s && %s == -1) ? (%s)%s : (%s)(%s / %s);\n", dst,
                        a, min_macro ? min_macro : "INT64_MIN", b, ctype,
                        min_macro ? min_macro : "INT64_MIN", ctype, a, b);
            } else {
                fprintf(out, "    %s = (%s == %s && %s == -1) ? (%s)0 : (%s)(%s %% %s);\n", dst,
                        a, min_macro ? min_macro : "INT64_MIN", b, ctype, ctype, a, b);
            }
        } else {
            fprintf(out, "    %s = (%s)(%s %s %s);\n", dst, ctype, a,
                    instr->binop == BIN_DIV ? "/" : "%", b);
        }
        break;
    }
    case BIN_AND:
    case BIN_OR:
    case BIN_XOR:
        fprintf(out, "    %s = (%s)(%s %s %s);\n", dst, ctype, a, c_binop(instr->binop), b);
        break;
    case BIN_SHL:
        fprintf(out, "    %s = (%s)((%s)%s << ((unsigned long long)%s & %d));\n", dst, ctype,
                utype, a, b, bits - 1);
        break;
    case BIN_SHR:
        if (is_signed) {
            fprintf(out, "    %s = (%s)(%s >> ((unsigned long long)%s & %d));\n", dst, ctype, a,
                    b, bits - 1);
        } else {
            fprintf(out, "    %s = (%s)((%s)%s >> ((unsigned long long)%s & %d));\n", dst, ctype,
                    utype, a, b, bits - 1);
        }
        break;
    default:
        codegen_failure(cg, (SourcePos){0, 1, 1, 0}, "unsupported binary operator");
        break;
    }
    RYN_UNUSED(fn);
}

static void emit_instr(Codegen *cg, FILE *out, const IrFunction *fn, const IrInstr *instr) {
    switch (instr->op) {
    case IR_CONST_I: {
        const char *dst = temp_name(cg, instr->dst);
        fprintf(out, "    %s = (%s)(", dst, c_type(cg, instr->type));
        emit_int_literal(out, instr->type, instr->ival, instr->uval);
        fputs(");\n", out);
        break;
    }
    case IR_CONST_F: {
        const char *dst = temp_name(cg, instr->dst);
        fprintf(out, "    %s = ", dst);
        emit_float_literal(out, instr->type, instr->fval);
        fputs(";\n", out);
        break;
    }
    case IR_CONST_STR: {
        const char *dst = temp_name(cg, instr->dst);
        fprintf(out, "    %s = ryn_str_%d;\n", dst, instr->string_id);
        break;
    }
    case IR_CONST_NULL:
        fprintf(out, "    %s = ((void *)0);\n", temp_name(cg, instr->dst));
        break;
    case IR_FUNC_PTR:
        fprintf(out, "    %s = %s;\n", temp_name(cg, instr->dst),
                instr->function ? instr->function->c_name : "((void *)0)");
        break;
    case IR_COPY:
        fprintf(out, "    %s = %s;\n", temp_name(cg, instr->dst), temp_name(cg, instr->a));
        break;
    case IR_BIN:
        emit_bin_instr(cg, out, fn, instr);
        break;
    case IR_CMP: {
        fprintf(out, "    %s = (bool)(%s %s %s);\n", temp_name(cg, instr->dst),
                temp_name(cg, instr->a), c_binop(instr->binop), temp_name(cg, instr->b));
        break;
    }
    case IR_STR_EQ:
        fprintf(out, "    %s = ryn_str_eq(%s, %s);\n", temp_name(cg, instr->dst),
                temp_name(cg, instr->a), temp_name(cg, instr->b));
        break;
    case IR_STR_INDEX:
        fprintf(out, "    %s = ryn_str_index(%s, (uint64_t)%s);\n", temp_name(cg, instr->dst),
                temp_name(cg, instr->a), temp_name(cg, instr->b));
        break;
    case IR_UN: {
        const char *dst = temp_name(cg, instr->dst);
        const char *a = temp_name(cg, instr->a);
        Type *type = instr->type;
        if (instr->unop == UN_NOT) {
            fprintf(out, "    %s = !%s;\n", dst, a);
        } else if (instr->unop == UN_NEG) {
            if (type_is_float(type)) {
                fprintf(out, "    %s = -%s;\n", dst, a);
            } else {
                const char *utype = unsigned_type_for(type);
                fprintf(out, "    %s = (%s)((%s)0 - (%s)%s);\n", dst, c_type(cg, type), utype,
                        utype, a);
            }
        } else if (instr->unop == UN_BNOT) {
            fprintf(out, "    %s = (%s)(~%s);\n", dst, c_type(cg, type), a);
        } else {
            codegen_failure(cg, (SourcePos){0, 1, 1, 0}, "unsupported unary operator");
        }
        break;
    }
    case IR_CALL:
    case IR_ICALL: {
        const char *callee = NULL;
        if (instr->op == IR_CALL) {
            callee = instr->builtin ? instr->builtin->c_name
                                    : (instr->function ? instr->function->c_name : NULL);
        } else {
            callee = temp_name(cg, instr->a);
        }
        if (!callee) {
            codegen_failure(cg, (SourcePos){0, 1, 1, 0}, "call without a target");
            return;
        }
        if (instr->dst >= 0) {
            fprintf(out, "    %s = %s(", temp_name(cg, instr->dst), callee);
        } else {
            fprintf(out, "    %s(", callee);
        }
        for (int i = 0; i < instr->argc; i++) {
            if (i) {
                fputs(", ", out);
            }
            bool by_value = false;
            if (instr->op == IR_CALL) {
                if (!instr->builtin && instr->function &&
                    (size_t)i < instr->function->nparams) {
                    by_value = type_is_aggregate(instr->function->param_types[i]);
                }
            } else {
                Type *callee_type = fn->temps[instr->a].type;
                if (callee_type && callee_type->kind == TY_PTR) {
                    callee_type = callee_type->as.ptr.elem;
                }
                if (callee_type && callee_type->kind == TY_FN &&
                    (size_t)i < callee_type->as.fn.nparams) {
                    by_value = type_is_aggregate(callee_type->as.fn.params[i]);
                }
            }
            if (by_value) {
                fprintf(out, "*%s", temp_name(cg, instr->args[i]));
            } else {
                fputs(temp_name(cg, instr->args[i]), out);
            }
        }
        fputs(");\n", out);
        break;
    }
    case IR_LOAD:
        fprintf(out, "    %s = *%s;\n", temp_name(cg, instr->dst), temp_name(cg, instr->a));
        break;
    case IR_STORE:
        fprintf(out, "    *%s = %s;\n", temp_name(cg, instr->a), temp_name(cg, instr->b));
        break;
    case IR_ADDR_LOCAL: {
        const char *local_name = local_c_name(cg, fn, (size_t)instr->local);
        fprintf(out, "    %s = &%s;\n", temp_name(cg, instr->dst), local_name);
        break;
    }
    case IR_ADDR_TEMP:
        fprintf(out, "    %s = &%s;\n", temp_name(cg, instr->dst), temp_name(cg, instr->a));
        break;
    case IR_LOCAL_READ:
        fprintf(out, "    %s = %s;\n", temp_name(cg, instr->dst),
                local_c_name(cg, fn, (size_t)instr->local));
        break;
    case IR_LOCAL_WRITE:
        fprintf(out, "    %s = %s;\n", local_c_name(cg, fn, (size_t)instr->local),
                temp_name(cg, instr->a));
        break;
    case IR_FIELD_ADDR: {
        Type *struct_type = fn->temps[instr->a].type;
        if (struct_type->kind == TY_PTR) {
            struct_type = struct_type->as.ptr.elem;
        }
        if (!struct_type || struct_type->kind != TY_STRUCT) {
            codegen_failure(cg, (SourcePos){0, 1, 1, 0}, "field access on a non struct pointer");
            return;
        }
        const char *field = sanitize_identifier(cg->arena,
                                                struct_type->as.strukt->fields[instr->field_index].name);
        fprintf(out, "    %s = &(%s->f_%s);\n", temp_name(cg, instr->dst),
                temp_name(cg, instr->a), field);
        break;
    }
    case IR_ELEM_ADDR: {
        Type *base_type = fn->temps[instr->a].type;
        if (base_type->kind == TY_PTR && base_type->as.ptr.elem->kind == TY_ARRAY) {
            fprintf(out, "    %s = &((*%s)[%s]);\n", temp_name(cg, instr->dst),
                    temp_name(cg, instr->a), temp_name(cg, instr->b));
        } else {
            fprintf(out, "    %s = &(%s[%s]);\n", temp_name(cg, instr->dst),
                    temp_name(cg, instr->a), temp_name(cg, instr->b));
        }
        break;
    }
    case IR_CAST:
        fprintf(out, "    %s = (%s)%s;\n", temp_name(cg, instr->dst), c_type(cg, instr->type),
                temp_name(cg, instr->a));
        break;
    case IR_AGG_COPY:
        fprintf(out, "    memcpy((void *)%s, (const void *)%s, sizeof(*%s));\n",
                temp_name(cg, instr->a), temp_name(cg, instr->b), temp_name(cg, instr->a));
        break;
    case IR_JMP:
        fprintf(out, "    goto bb%d;\n", instr->a);
        break;
    case IR_BR:
        fprintf(out, "    if (%s) goto bb%d; else goto bb%d;\n", temp_name(cg, instr->a),
                instr->b, instr->c);
        break;
    case IR_RET: {
        bool aggregate = type_is_aggregate(fn->return_type);
        if (instr->a < 0) {
            fputs("    return;\n", out);
        } else if (aggregate) {
            fprintf(out, "    return *%s;\n", temp_name(cg, instr->a));
        } else if (type_is_error(fn->return_type) && instr->type &&
                   type_is_aggregate(instr->type)) {
            fprintf(out, "    return *%s;\n", temp_name(cg, instr->a));
        } else {
            fprintf(out, "    return %s;\n", temp_name(cg, instr->a));
        }
        break;
    }
    case IR_NOP:
        fputs("    ;\n", out);
        break;
    default:
        codegen_failure(cg, (SourcePos){0, 1, 1, 0}, "unsupported IR instruction");
        break;
    }
}

static void emit_signature(Codegen *cg, FILE *out, const IrFunction *fn) {
    fputs(c_decl(cg, fn->return_type, fn->c_name), out);
    fputc('(', out);
    size_t emitted = 0;
    for (size_t i = 0; i < fn->nlocals; i++) {
        if (!fn->locals[i].is_param) {
            continue;
        }
        if (emitted++) {
            fputs(", ", out);
        }
        fputs(c_decl(cg, fn->locals[i].type, local_c_name(cg, fn, i)), out);
    }
    if (emitted == 0) {
        fputs("void", out);
    }
    fputc(')', out);
}

static void emit_function(Codegen *cg, FILE *out, const IrFunction *fn) {
    fputs("/* --- ", out);
    fputs(fn->name, out);
    fputs(" --- */\n", out);
    emit_signature(cg, out, fn);
    fputs(" {\n", out);
    /* Local declarations (parameters use the names from the signature). */
    for (size_t i = 0; i < fn->nlocals; i++) {
        if (fn->locals[i].is_param) {
            continue;
        }
        const char *name = local_c_name(cg, fn, i);
        bool aggregate = type_is_aggregate(fn->locals[i].type);
        fprintf(out, "    %s%s\n", c_decl(cg, fn->locals[i].type, name),
                aggregate ? " = {0};" : " = 0;");
    }
    for (size_t i = 0; i < fn->ntemps; i++) {
        const char *name = temp_name(cg, (int)i);
        fprintf(out, "    %s;\n", c_decl(cg, fn->temps[i].type, name));
    }
    if (fn->nblocks > 0) {
        fputs("    goto bb0;\n", out);
    }
    for (size_t b = 0; b < fn->nblocks; b++) {
        const IrBlock *block = fn->blocks[b];
        fprintf(out, "\nbb%d:;\n", block->id);
        for (const IrInstr *instr = block->first; instr; instr = instr->next) {
            emit_instr(cg, out, fn, instr);
        }
        bool terminated = block->last && (block->last->op == IR_JMP || block->last->op == IR_BR ||
                                          block->last->op == IR_RET);
        if (!terminated && b + 1 < fn->nblocks) {
            fprintf(out, "    goto bb%d;\n", fn->blocks[b + 1]->id);
        }
    }
    fputs("}\n\n", out);
}

/* ------------------------------------------------------------------ */
/* Top level                                                          */
/* ------------------------------------------------------------------ */

bool codegen_emit(FILE *out, Arena *arena, Diagnostics *diag, AstProgram *program, IrModule *module,
                  TypeContext *types, const CodegenOptions *options) {
    Codegen cg;
    memset(&cg, 0, sizeof(cg));
    cg.arena = arena;
    cg.diag = diag;
    cg.program = program;
    cg.module = module;
    cg.types = types;
    cg.options = options;

    /* Discover every type that needs a declaration before emitting code. */
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind == AST_STRUCT_DECL) {
            AstStructDecl *st = (AstStructDecl *)decl;
            if (st->info) {
                for (size_t f = 0; f < st->info->nfields; f++) {
                    collect_types(&cg, st->info->fields[f].type);
                }
            }
        } else if (decl->kind == AST_FN_DECL) {
            AstFnDecl *fn = (AstFnDecl *)decl;
            if (fn->info) {
                collect_types(&cg, fn->info->return_type);
                for (size_t p = 0; p < fn->info->nparams; p++) {
                    collect_types(&cg, fn->info->param_types[p]);
                }
            }
        }
    }
    for (size_t i = 0; i < module->nfunctions; i++) {
        const IrFunction *fn = module->functions[i];
        collect_types(&cg, fn->return_type);
        for (size_t l = 0; l < fn->nlocals; l++) {
            collect_types(&cg, fn->locals[l].type);
        }
        for (size_t t = 0; t < fn->ntemps; t++) {
            collect_types(&cg, fn->temps[t].type);
        }
    }

    fprintf(out, "/* Generated by the Ryn compiler %s from %s.\n", "0.1.0",
            options && options->source_path ? options->source_path : "<source>");
    fputs(" * This file is C11 and may be compiled directly with a C compiler.\n */\n\n", out);
    if (options && options->nincludes) {
        for (size_t i = 0; i < options->nincludes; i++) {
            const char *include = options->includes[i];
            if (include[0] == '<' || include[0] == '"') {
                fprintf(out, "#include %s\n", include);
            } else {
                fprintf(out, "#include <%s>\n", include);
            }
        }
        fputc('\n', out);
    }
    fputs("#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n"
          "#include <string.h>\n\n#include \"ryn_runtime.h\"\n\n",
          out);

    /* Forward declarations of named types. */
    size_t struct_count = 0;
    for (size_t i = 0; i < program->decls.len; i++) {
        if (program->decls.items[i]->kind == AST_STRUCT_DECL) {
            struct_count++;
        }
    }
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind == AST_STRUCT_DECL) {
            AstStructDecl *st = (AstStructDecl *)decl;
            fprintf(out, "typedef struct %s %s;\n", struct_c_name(&cg, st->info),
                    struct_c_name(&cg, st->info));
        } else if (decl->kind == AST_ENUM_DECL) {
            AstEnumDecl *en = (AstEnumDecl *)decl;
            fprintf(out, "typedef %s %s;\n", en->info->is_wide ? "int64_t" : "int32_t",
                    enum_c_name(&cg, en->info));
        }
    }
    fputc('\n', out);

    /* Function pointer typedefs. They are emitted in reverse registration
     * order so that a function type that mentions another one sees its
     * typedef first. */
    for (size_t i = cg.nfn_types; i-- > 0;) {
        Type *fn = cg.fn_types[i].type;
        fprintf(out, "typedef %s (*%s)(", c_base_type(&cg, fn->as.fn.ret),
                cg.fn_types[i].name);
        if (fn->as.fn.nparams == 0) {
            fputs("void", out);
        }
        for (size_t p = 0; p < fn->as.fn.nparams; p++) {
            if (p) {
                fputs(", ", out);
            }
            fputs(c_param_type(&cg, fn->as.fn.params[p]), out);
        }
        fputs(");\n", out);
    }
    if (cg.nfn_types) {
        fputc('\n', out);
    }

    /* Struct definitions in dependency order. */
    EmitState state;
    state.state = ARENA_ARRAY(arena, int, program->decls.len);
    state.count = program->decls.len;
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind == AST_STRUCT_DECL) {
            emit_struct_definition(&cg, out, ((AstStructDecl *)decl)->info, &state);
        }
    }

    /* String literals. */
    for (size_t i = 0; i < module->nstrings; i++) {
        fprintf(out, "static const char ryn_str_%zu[] = ", i);
        emit_c_string(out, module->strings[i].value, module->strings[i].length);
        fputs(";\n", out);
    }
    if (module->nstrings) {
        fputc('\n', out);
    }

    /* Prototypes: user functions and extern declarations. */
    FunctionInfo *main_fn = NULL;
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind != AST_FN_DECL) {
            continue;
        }
        AstFnDecl *fn = (AstFnDecl *)decl;
        if (!fn->info) {
            continue;
        }
        if (strcmp(fn->name, "main") == 0) {
            main_fn = fn->info;
        }
        fprintf(out, "%s%s(", fn->is_extern ? "extern " : "",
                c_decl(&cg, fn->info->return_type, fn->info->c_name));
        if (fn->info->nparams == 0) {
            fputs("void", out);
        }
        for (size_t p = 0; p < fn->info->nparams; p++) {
            if (p) {
                fputs(", ", out);
            }
            fputs(c_param_type(&cg, fn->info->param_types[p]), out);
        }
        fputs(");\n", out);
    }
    fputc('\n', out);

    /* Function definitions. */
    for (size_t i = 0; i < module->nfunctions; i++) {
        emit_function(&cg, out, module->functions[i]);
    }

    /* Entry point wrapper. */
    if (main_fn) {
        fputs("int main(void) {\n", out);
        if (main_fn->return_type->kind == TY_VOID) {
            fprintf(out, "    %s();\n    return 0;\n", main_fn->c_name);
        } else {
            fprintf(out, "    return (int)%s();\n", main_fn->c_name);
        }
        fputs("}\n", out);
    }

    free(cg.fn_types);
    free(cg.visited_structs);
    return !cg.failed;
}
