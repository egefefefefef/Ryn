#include "builtins.h"

#define BF0(name_, cname_, ret_) \
    { name_, cname_, {BSPEC_VOID}, 0, ret_ }
#define BF1(name_, cname_, p0_, ret_) \
    { name_, cname_, {p0_}, 1, ret_ }
#define BF2(name_, cname_, p0_, p1_, ret_) \
    { name_, cname_, {p0_, p1_}, 2, ret_ }
#define BF3(name_, cname_, p0_, p1_, p2_, ret_) \
    { name_, cname_, {p0_, p1_, p2_}, 3, ret_ }

/* ------------------------------------------------------------------ */
/* print / println overloads (global and io)                          */
/* ------------------------------------------------------------------ */

#define PRINT_ENTRY(spec, suffix) \
    BF1("print", "ryn_print_" suffix, spec, BSPEC_VOID)
#define PRINTLN_ENTRY(spec, suffix) \
    BF1("println", "ryn_println_" suffix, spec, BSPEC_VOID)

static const BuiltinFn GLOBAL_FNS[] = {
    PRINT_ENTRY(BSPEC_I8, "i8"),
    PRINT_ENTRY(BSPEC_I16, "i16"),
    PRINT_ENTRY(BSPEC_I32, "i32"),
    PRINT_ENTRY(BSPEC_I64, "i64"),
    PRINT_ENTRY(BSPEC_U8, "u8"),
    PRINT_ENTRY(BSPEC_U16, "u16"),
    PRINT_ENTRY(BSPEC_U32, "u32"),
    PRINT_ENTRY(BSPEC_U64, "u64"),
    PRINT_ENTRY(BSPEC_F32, "f32"),
    PRINT_ENTRY(BSPEC_F64, "f64"),
    PRINT_ENTRY(BSPEC_BOOL, "bool"),
    PRINT_ENTRY(BSPEC_CHAR, "char"),
    PRINT_ENTRY(BSPEC_STR, "str"),
    PRINT_ENTRY(BSPEC_ANY_PTR, "ptr"),

    PRINTLN_ENTRY(BSPEC_I8, "i8"),
    PRINTLN_ENTRY(BSPEC_I16, "i16"),
    PRINTLN_ENTRY(BSPEC_I32, "i32"),
    PRINTLN_ENTRY(BSPEC_I64, "i64"),
    PRINTLN_ENTRY(BSPEC_U8, "u8"),
    PRINTLN_ENTRY(BSPEC_U16, "u16"),
    PRINTLN_ENTRY(BSPEC_U32, "u32"),
    PRINTLN_ENTRY(BSPEC_U64, "u64"),
    PRINTLN_ENTRY(BSPEC_F32, "f32"),
    PRINTLN_ENTRY(BSPEC_F64, "f64"),
    PRINTLN_ENTRY(BSPEC_BOOL, "bool"),
    PRINTLN_ENTRY(BSPEC_CHAR, "char"),
    PRINTLN_ENTRY(BSPEC_STR, "str"),
    PRINTLN_ENTRY(BSPEC_ANY_PTR, "ptr"),

    BF1("panic", "ryn_panic", BSPEC_STR, BSPEC_VOID),
    BF1("assert", "ryn_assert", BSPEC_BOOL, BSPEC_VOID),
    BF2("assert_msg", "ryn_assert_msg", BSPEC_BOOL, BSPEC_STR, BSPEC_VOID),
    BF1("len", "ryn_str_len", BSPEC_STR, BSPEC_U64),
};

/* ------------------------------------------------------------------ */
/* io                                                                 */
/* ------------------------------------------------------------------ */

static const BuiltinFn IO_FNS[] = {
    BF0("read_line", "ryn_io_read_line", BSPEC_STR),
    BF0("read_int", "ryn_io_read_int", BSPEC_I32),
    BF0("read_float", "ryn_io_read_float", BSPEC_F64),
    BF0("flush", "ryn_io_flush", BSPEC_VOID),
};

/* ------------------------------------------------------------------ */
/* math                                                               */
/* ------------------------------------------------------------------ */

#define MATH1(ryn_name, c_suffix, ret) \
    BF1(#ryn_name, "ryn_math_" c_suffix, BSPEC_F64, ret)
#define MATH1I(ryn_name, c_suffix, arg, ret) \
    BF1(#ryn_name, "ryn_math_" c_suffix, arg, ret)

static const BuiltinFn MATH_FNS[] = {
    MATH1(sqrt, "sqrt", BSPEC_F64),
    MATH1(cbrt, "cbrt", BSPEC_F64),
    MATH1(sin, "sin", BSPEC_F64),
    MATH1(cos, "cos", BSPEC_F64),
    MATH1(tan, "tan", BSPEC_F64),
    MATH1(asin, "asin", BSPEC_F64),
    MATH1(acos, "acos", BSPEC_F64),
    MATH1(atan, "atan", BSPEC_F64),
    MATH1(exp, "exp", BSPEC_F64),
    MATH1(ln, "ln", BSPEC_F64),
    MATH1(log2, "log2", BSPEC_F64),
    MATH1(log10, "log10", BSPEC_F64),
    MATH1(floor, "floor", BSPEC_F64),
    MATH1(ceil, "ceil", BSPEC_F64),
    MATH1(round, "round", BSPEC_F64),
    MATH1(trunc, "trunc", BSPEC_F64),
    MATH1(fabs, "fabs", BSPEC_F64),
    BF2("pow", "ryn_math_pow", BSPEC_F64, BSPEC_F64, BSPEC_F64),
    BF2("atan2", "ryn_math_atan2", BSPEC_F64, BSPEC_F64, BSPEC_F64),
    BF2("fmod", "ryn_math_fmod", BSPEC_F64, BSPEC_F64, BSPEC_F64),
    BF2("hypot", "ryn_math_hypot", BSPEC_F64, BSPEC_F64, BSPEC_F64),
    BF2("min", "ryn_math_min_f64", BSPEC_F64, BSPEC_F64, BSPEC_F64),
    BF2("max", "ryn_math_max_f64", BSPEC_F64, BSPEC_F64, BSPEC_F64),
    BF2("min", "ryn_math_min_i32", BSPEC_I32, BSPEC_I32, BSPEC_I32),
    BF2("max", "ryn_math_max_i32", BSPEC_I32, BSPEC_I32, BSPEC_I32),
    BF2("min", "ryn_math_min_i64", BSPEC_I64, BSPEC_I64, BSPEC_I64),
    BF2("max", "ryn_math_max_i64", BSPEC_I64, BSPEC_I64, BSPEC_I64),
    MATH1I(abs, "abs_i32", BSPEC_I32, BSPEC_I32),
    MATH1I(abs, "abs_i64", BSPEC_I64, BSPEC_I64),
};

static const BuiltinConst MATH_CONSTS[] = {
    {"PI", BSPEC_F64, false, 0, 3.14159265358979323846},
    {"E", BSPEC_F64, false, 0, 2.71828182845904523536},
    {"TAU", BSPEC_F64, false, 0, 6.28318530717958647692},
    {"INF", BSPEC_F64, false, 0, (double)1.0 / 0.0},
    {"NAN", BSPEC_F64, false, 0, (double)0.0 / 0.0},
};

/* ------------------------------------------------------------------ */
/* str                                                                */
/* ------------------------------------------------------------------ */

static const BuiltinFn STR_FNS[] = {
    BF1("len", "ryn_str_len", BSPEC_STR, BSPEC_U64),
    BF2("eq", "ryn_str_eq", BSPEC_STR, BSPEC_STR, BSPEC_BOOL),
    BF2("concat", "ryn_str_concat", BSPEC_STR, BSPEC_STR, BSPEC_STR),
    BF1("from_int", "ryn_str_from_int", BSPEC_I64, BSPEC_STR),
    BF1("free", "ryn_str_free", BSPEC_STR, BSPEC_VOID),
};

/* ------------------------------------------------------------------ */
/* mem                                                                */
/* ------------------------------------------------------------------ */

static const BuiltinFn MEM_FNS[] = {
    BF1("alloc", "ryn_mem_alloc", BSPEC_U64, BSPEC_ANY_PTR),
    BF1("free", "ryn_mem_free", BSPEC_ANY_PTR, BSPEC_VOID),
    BF2("realloc", "ryn_mem_realloc", BSPEC_ANY_PTR, BSPEC_U64, BSPEC_ANY_PTR),
    BF3("copy", "ryn_mem_copy", BSPEC_ANY_PTR, BSPEC_ANY_PTR, BSPEC_U64, BSPEC_VOID),
    BF2("zero", "ryn_mem_zero", BSPEC_ANY_PTR, BSPEC_U64, BSPEC_VOID),
};

/* ------------------------------------------------------------------ */
/* core                                                               */
/* ------------------------------------------------------------------ */

static const BuiltinModule MODULES[] = {
    {"core", "essential operations available in every program", GLOBAL_FNS,
     sizeof(GLOBAL_FNS) / sizeof(GLOBAL_FNS[0]), NULL, 0},
    {"io", "console input and output", IO_FNS, sizeof(IO_FNS) / sizeof(IO_FNS[0]), NULL, 0},
    {"math", "mathematical functions and constants", MATH_FNS,
     sizeof(MATH_FNS) / sizeof(MATH_FNS[0]), MATH_CONSTS,
     sizeof(MATH_CONSTS) / sizeof(MATH_CONSTS[0])},
    {"strings", "string utilities", STR_FNS, sizeof(STR_FNS) / sizeof(STR_FNS[0]), NULL, 0},
    {"mem", "raw memory management", MEM_FNS, sizeof(MEM_FNS) / sizeof(MEM_FNS[0]), NULL, 0},
};

const BuiltinModule *stdlib_modules(size_t *out_count) {
    if (out_count) {
        *out_count = sizeof(MODULES) / sizeof(MODULES[0]);
    }
    return MODULES;
}

const BuiltinModule *stdlib_find_module(const char *name) {
    for (size_t i = 0; i < sizeof(MODULES) / sizeof(MODULES[0]); i++) {
        if (strcmp(MODULES[i].name, name) == 0) {
            return &MODULES[i];
        }
    }
    return NULL;
}

static size_t collect_from(const BuiltinFn *fns, size_t count, const char *name,
                           const BuiltinFn **out, size_t max_out) {
    size_t written = 0;
    for (size_t i = 0; i < count && written < max_out; i++) {
        if (strcmp(fns[i].name, name) == 0) {
            out[written++] = &fns[i];
        }
    }
    return written;
}

size_t stdlib_candidates(const BuiltinModule *module, const char *name, const BuiltinFn **out,
                         size_t max_out) {
    if (!module) {
        return collect_from(GLOBAL_FNS, sizeof(GLOBAL_FNS) / sizeof(GLOBAL_FNS[0]), name, out,
                            max_out);
    }
    size_t written = collect_from(module->functions, module->nfunctions, name, out, max_out);
    /* io and core also expose the global print family. */
    if (strcmp(module->name, "io") == 0 &&
        (strcmp(name, "print") == 0 || strcmp(name, "println") == 0)) {
        written += collect_from(GLOBAL_FNS, sizeof(GLOBAL_FNS) / sizeof(GLOBAL_FNS[0]), name,
                                out + written, max_out - written);
    }
    return written;
}

Type *stdlib_spec_type(TypeContext *ctx, BuiltinTypeSpec spec) {
    switch (spec) {
    case BSPEC_VOID: return type_prim(ctx, TY_VOID);
    case BSPEC_BOOL: return type_prim(ctx, TY_BOOL);
    case BSPEC_CHAR: return type_prim(ctx, TY_CHAR);
    case BSPEC_STR: return type_prim(ctx, TY_STR);
    case BSPEC_I8: return type_prim(ctx, TY_I8);
    case BSPEC_I16: return type_prim(ctx, TY_I16);
    case BSPEC_I32: return type_prim(ctx, TY_I32);
    case BSPEC_I64: return type_prim(ctx, TY_I64);
    case BSPEC_U8: return type_prim(ctx, TY_U8);
    case BSPEC_U16: return type_prim(ctx, TY_U16);
    case BSPEC_U32: return type_prim(ctx, TY_U32);
    case BSPEC_U64: return type_prim(ctx, TY_U64);
    case BSPEC_F32: return type_prim(ctx, TY_F32);
    case BSPEC_F64: return type_prim(ctx, TY_F64);
    case BSPEC_ANY_PTR: return type_ptr(ctx->arena, type_prim(ctx, TY_VOID));
    case BSPEC_ANY_INT: return type_prim(ctx, TY_I64);     /* scoring only */
    case BSPEC_ANY_NUMERIC: return type_prim(ctx, TY_F64); /* scoring only */
    default: return NULL;
    }
}

const char *stdlib_spec_name(BuiltinTypeSpec spec) {
    switch (spec) {
    case BSPEC_VOID: return "void";
    case BSPEC_BOOL: return "bool";
    case BSPEC_CHAR: return "char";
    case BSPEC_STR: return "str";
    case BSPEC_I8: return "i8";
    case BSPEC_I16: return "i16";
    case BSPEC_I32: return "i32";
    case BSPEC_I64: return "i64";
    case BSPEC_U8: return "u8";
    case BSPEC_U16: return "u16";
    case BSPEC_U32: return "u32";
    case BSPEC_U64: return "u64";
    case BSPEC_F32: return "f32";
    case BSPEC_F64: return "f64";
    case BSPEC_ANY_PTR: return "ptr";
    case BSPEC_ANY_INT: return "integer";
    case BSPEC_ANY_NUMERIC: return "number";
    default: return "<invalid>";
    }
}

bool stdlib_spec_matches(BuiltinTypeSpec spec, const Type *type, int *score) {
    int local = 0;
    bool ok = false;
    if (type_is_error(type)) {
        *score = 3;
        return true;
    }
    if (type->kind == TY_UNTYPED_INT) {
        switch (spec) {
        case BSPEC_I8:
        case BSPEC_I16:
        case BSPEC_I32:
        case BSPEC_I64:
        case BSPEC_U8:
        case BSPEC_U16:
        case BSPEC_U32:
        case BSPEC_U64:
        case BSPEC_F32:
        case BSPEC_F64:
        case BSPEC_ANY_INT:
        case BSPEC_ANY_NUMERIC:
            local = 2;
            ok = true;
            break;
        default:
            break;
        }
    } else if (type->kind == TY_UNTYPED_FLOAT) {
        if (spec == BSPEC_F32 || spec == BSPEC_F64 || spec == BSPEC_ANY_NUMERIC) {
            local = 2;
            ok = true;
        }
    } else if (type->kind == TY_NULL) {
        if (spec == BSPEC_ANY_PTR) {
            local = 3;
            ok = true;
        }
    } else {
        Type *wanted = NULL;
        if (spec >= BSPEC_BOOL && spec <= BSPEC_F64) {
            wanted = NULL; /* resolved by the caller's TypeContext, see below */
        }
        switch (spec) {
        case BSPEC_BOOL: ok = type->kind == TY_BOOL; break;
        case BSPEC_CHAR: ok = type->kind == TY_CHAR; break;
        case BSPEC_STR: ok = type->kind == TY_STR; break;
        case BSPEC_I8: ok = type->kind == TY_I8; break;
        case BSPEC_I16: ok = type->kind == TY_I16; break;
        case BSPEC_I32: ok = type->kind == TY_I32; break;
        case BSPEC_I64: ok = type->kind == TY_I64; break;
        case BSPEC_U8: ok = type->kind == TY_U8; break;
        case BSPEC_U16: ok = type->kind == TY_U16; break;
        case BSPEC_U32: ok = type->kind == TY_U32; break;
        case BSPEC_U64: ok = type->kind == TY_U64; break;
        case BSPEC_F32: ok = type->kind == TY_F32; break;
        case BSPEC_F64: ok = type->kind == TY_F64; break;
        case BSPEC_ANY_PTR: ok = type->kind == TY_PTR || type->kind == TY_NULL; break;
        case BSPEC_ANY_INT: ok = type_is_integer(type); break;
        case BSPEC_ANY_NUMERIC: ok = type_is_numeric(type); break;
        case BSPEC_VOID: ok = type->kind == TY_VOID; break;
        default: ok = false; break;
        }
        if (ok) {
            local = (spec == BSPEC_ANY_PTR || spec == BSPEC_ANY_INT ||
                     spec == BSPEC_ANY_NUMERIC)
                        ? 1
                        : 3;
        }
        RYN_UNUSED(wanted);
    }
    if (ok) {
        *score = local;
    }
    return ok;
}

const char *stdlib_module_names_csv(void) {
    static char buffer[128];
    buffer[0] = '\0';
    for (size_t i = 0; i < sizeof(MODULES) / sizeof(MODULES[0]); i++) {
        if (i) {
            strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, MODULES[i].name, sizeof(buffer) - strlen(buffer) - 1);
    }
    return buffer;
}
