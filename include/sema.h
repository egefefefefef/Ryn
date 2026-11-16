/*
 * Ryn programming language — semantic analysis.
 *
 * Responsibilities:
 *   - collect struct/enum/function declarations and validate duplicates;
 *   - resolve syntactic types to semantic types;
 *   - check every function body: scopes, mutability, definite assignment,
 *     type compatibility, calls, control flow and reachability;
 *   - annotate the AST for the IR builder and the C backend.
 */
#ifndef RYN_SEMA_H
#define RYN_SEMA_H

#include "ast.h"
#include "diagnostics.h"
#include "builtins.h"
#include "types.h"

typedef struct SemaScope SemaScope;

typedef enum SemaSymbolKind {
    SYM_VARIABLE = 1,
    SYM_MODULE
} SemaSymbolKind;

typedef struct SemaSymbol {
    const char *name;
    SemaSymbolKind kind;
    Type *type;
    bool is_mutable;
    bool initialized;
    bool read;
    bool is_param;
    bool is_loop_var;
    SourcePos pos;
    const BuiltinModule *module;
    SemaScope *scope;
    struct SemaSymbol *next_in_scope;
    struct SemaSymbol *next_local;
} SemaSymbol;

/* Describes a function (user defined, extern or builtin-backed). */
struct FunctionInfo {
    const char *name;
    const char *c_name;
    Type **param_types;
    size_t nparams;
    Type *return_type;
    Type *fn_type;
    SourcePos pos;
    bool is_extern;
    AstFnDecl *decl;
    int index;
};

RYN_DEFINE_VEC(FunctionInfoVec, FunctionInfo *)
RYN_DEFINE_VEC(StructInfoVec, StructInfo *)
RYN_DEFINE_VEC(EnumInfoVec, EnumInfo *)
RYN_DEFINE_VEC(BuiltinModuleVec, const BuiltinModule *)

typedef struct Sema {
    Arena *arena;
    Diagnostics *diag;
    SourceManager *sm;
    TypeContext types;
    AstProgram *program;

    FunctionInfoVec functions;
    StructInfoVec structs;
    EnumInfoVec enums;
    BuiltinModuleVec imports;

    SemaScope *global_scope;
    SemaScope *scope;
    SemaSymbol *locals_head;

    FunctionInfo *current_function;
    bool lvalue_mode; /* while checking assignment targets, do not read the base */
    int loop_depth;
    int function_depth;
    bool had_error;
} Sema;

void sema_init(Sema *sema, Arena *arena, Diagnostics *diag, SourceManager *sm);
void sema_free(Sema *sema);

/* Runs all semantic passes. Returns false when errors were reported. */
bool sema_analyze(Sema *sema, AstProgram *program);

FunctionInfo *sema_lookup_function(Sema *sema, const char *name);
StructInfo *sema_lookup_struct(Sema *sema, const char *name);
EnumInfo *sema_lookup_enum(Sema *sema, const char *name);

#endif /* RYN_SEMA_H */
