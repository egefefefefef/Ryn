/*
 * Ryn programming language — standard library registry.
 *
 * The first version of the standard library is implemented in C (runtime/)
 * and exposed through builtin modules. Every function listed here has a real
 * implementation; there are no placeholders. A future version will allow
 * modules written in Ryn itself on top of these primitives.
 *
 * Modules:
 *   core — always-available basics: print, println, panic, assert, len
 *   io   — console input/output
 *   math — mathematical functions and constants
 *   strings — string helpers
 *   mem  — raw memory management
 */
#ifndef RYN_STDLIB_H
#define RYN_STDLIB_H

#include "common.h"
#include "types.h"

/* A parameter type in a builtin signature. */
typedef enum BuiltinTypeSpec {
    BSPEC_VOID = 0,
    BSPEC_BOOL,
    BSPEC_CHAR,
    BSPEC_STR,
    BSPEC_I8,
    BSPEC_I16,
    BSPEC_I32,
    BSPEC_I64,
    BSPEC_U8,
    BSPEC_U16,
    BSPEC_U32,
    BSPEC_U64,
    BSPEC_F32,
    BSPEC_F64,
    BSPEC_ANY_PTR,      /* any pointer type, or null */
    BSPEC_ANY_INT,      /* any integer type */
    BSPEC_ANY_NUMERIC   /* any integer or float type */
} BuiltinTypeSpec;

typedef struct BuiltinFn {
    const char *name;   /* name visible in Ryn, e.g. "println" */
    const char *c_name; /* runtime symbol, e.g. "ryn_println_i32" */
    BuiltinTypeSpec params[4];
    size_t nparams;
    BuiltinTypeSpec ret;
} BuiltinFn;

typedef struct BuiltinConst {
    const char *name;
    BuiltinTypeSpec type;
    bool is_integer;
    int64_t ivalue;
    double fvalue;
} BuiltinConst;

typedef struct BuiltinModule {
    const char *name;
    const char *summary;
    const BuiltinFn *functions;
    size_t nfunctions;
    const BuiltinConst *constants;
    size_t nconstants;
} BuiltinModule;

/* All modules known to the compiler. */
const BuiltinModule *stdlib_modules(size_t *out_count);
const BuiltinModule *stdlib_find_module(const char *name);

/* Collects functions named 'name' from a module (module == NULL means the
 * always-available global namespace). Returns the number written to out. */
size_t stdlib_candidates(const BuiltinModule *module, const char *name, const BuiltinFn **out,
                         size_t max_out);

/* Type helpers. */
Type *stdlib_spec_type(TypeContext *ctx, BuiltinTypeSpec spec);
const char *stdlib_spec_name(BuiltinTypeSpec spec);
bool stdlib_spec_matches(BuiltinTypeSpec spec, const Type *type, int *score);
const char *stdlib_module_names_csv(void);

#endif /* RYN_STDLIB_H */
