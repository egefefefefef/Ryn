/* Internal interface shared by the semantic analysis translation units. */
#ifndef RYN_SEMA_INTERNAL_H
#define RYN_SEMA_INTERNAL_H

#include "sema.h"

typedef struct ExprValue {
    Type *type;
    bool is_lvalue;
    bool is_mutable;
} ExprValue;

typedef struct FlowEntry {
    SemaSymbol *symbol;
    bool initialized;
} FlowEntry;

typedef struct FlowState {
    FlowEntry *items;
    size_t len;
    size_t cap;
} FlowState;

/* Scopes ------------------------------------------------------------- */
SemaScope *sema_scope_push(Sema *sema, bool function_scope);
void sema_scope_pop(Sema *sema);
SemaSymbol *sema_scope_define(Sema *sema, const char *name, Type *type, bool is_mutable,
                              SourcePos pos, bool is_param);
SemaSymbol *sema_scope_lookup(Sema *sema, const char *name);
SemaSymbol *sema_scope_lookup_current(Sema *sema, const char *name);
void sema_scope_add_module(Sema *sema, const BuiltinModule *module, SourcePos pos);

/* Diagnostics -------------------------------------------------------- */
void sema_error(Sema *sema, SourcePos pos, int span, DiagCode code, const char *fmt, ...);
void sema_error_note(Sema *sema, SourcePos pos, int span, DiagCode code, const char *note,
                     const char *fmt, ...) ;
void sema_warn(Sema *sema, SourcePos pos, int span, DiagCode code, const char *fmt, ...);
void sema_warn_note(Sema *sema, SourcePos pos, int span, DiagCode code, const char *note,
                    const char *fmt, ...);

/* Types -------------------------------------------------------------- */
Type *sema_resolve_type(Sema *sema, AstType *type);
bool sema_int_literal_fits(Type *target, int64_t value, uint64_t uvalue);
void sema_materialize_default(Sema *sema, AstExpr *expr, Type *chosen);
void sema_default_expr_type(Sema *sema, AstExpr *expr, ExprValue *value);
bool sema_coerce(Sema *sema, AstExpr *expr, ExprValue *value, Type *target, const char *context);

/* Expressions and statements ----------------------------------------- */
void sema_check_expr(Sema *sema, AstExpr *expr, Type *expected, ExprValue *out);
void sema_check_condition(Sema *sema, AstExpr *expr);
void sema_check_block(Sema *sema, AstBlock *block);
void sema_check_stmt(Sema *sema, AstStmt *stmt);
void sema_check_function(Sema *sema, AstFnDecl *decl);
bool sema_block_always_returns(const AstBlock *block);
bool sema_stmt_exits(const AstStmt *stmt);
void sema_mark_initialized(AstExpr *target);

/* Flow --------------------------------------------------------------- */
void sema_flow_capture(Sema *sema, FlowState *state);
void sema_flow_restore(const FlowState *state);
void sema_flow_merge_and(FlowState *into, const FlowState *other);
void sema_flow_free(FlowState *state);

/* Builtin argument matching ------------------------------------------ */
int sema_builtin_arg_score(Sema *sema, BuiltinTypeSpec spec, AstExpr *arg, const Type *arg_type);

#endif /* RYN_SEMA_INTERNAL_H */
