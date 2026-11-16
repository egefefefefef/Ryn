/*
 * Ryn programming language — abstract syntax tree.
 *
 * Nodes are allocated in an arena and are immutable after parsing; the
 * semantic analyzer annotates them in place (resolved types, symbols).
 */
#ifndef RYN_AST_H
#define RYN_AST_H

#include "common.h"
#include "source.h"
#include "types.h"

typedef struct SemaSymbol SemaSymbol;
typedef struct FunctionInfo FunctionInfo;
typedef struct BuiltinFn BuiltinFn;
typedef struct BuiltinConst BuiltinConst;

/* ------------------------------------------------------------------ */
/* Operators                                                          */
/* ------------------------------------------------------------------ */

typedef enum UnOp {
    UN_NEG = 1,
    UN_NOT,
    UN_BNOT,
    UN_ADDR,
    UN_DEREF
} UnOp;

typedef enum BinOp {
    BIN_NONE = 0,
    BIN_ADD,
    BIN_SUB,
    BIN_MUL,
    BIN_DIV,
    BIN_MOD,
    BIN_AND,
    BIN_OR,
    BIN_XOR,
    BIN_SHL,
    BIN_SHR,
    BIN_EQ,
    BIN_NE,
    BIN_LT,
    BIN_LE,
    BIN_GT,
    BIN_GE,
    BIN_LAND,
    BIN_LOR
} BinOp;

const char *unop_name(UnOp op);
const char *binop_name(BinOp op);
bool binop_is_comparison(BinOp op);
bool binop_is_logical(BinOp op);
bool binop_is_arithmetic(BinOp op);

/* ------------------------------------------------------------------ */
/* Syntactic types                                                    */
/* ------------------------------------------------------------------ */

typedef enum AstTypeKind {
    AST_TYPE_NAME = 1,
    AST_TYPE_PTR,
    AST_TYPE_ARRAY,
    AST_TYPE_FN
} AstTypeKind;

typedef struct AstType {
    AstTypeKind kind;
    SourcePos pos;
    const char *name;          /* AST_TYPE_NAME */
    struct AstType *elem;      /* pointer target / array element */
    int64_t array_length;      /* AST_TYPE_ARRAY */
    struct AstType **params;   /* AST_TYPE_FN */
    size_t nparams;
    struct AstType *ret;       /* AST_TYPE_FN */
    Type *resolved;            /* sema annotation */
} AstType;

/* ------------------------------------------------------------------ */
/* Expressions                                                        */
/* ------------------------------------------------------------------ */

typedef enum NodeKind {
    /* Expressions */
    AST_INT_LITERAL = 1,
    AST_FLOAT_LITERAL,
    AST_STRING_LITERAL,
    AST_CHAR_LITERAL,
    AST_BOOL_LITERAL,
    AST_NULL_LITERAL,
    AST_IDENT,
    AST_UNARY,
    AST_BINARY,
    AST_CALL,
    AST_FIELD,
    AST_INDEX,
    AST_CAST,
    AST_STRUCT_LITERAL,
    AST_ARRAY_LITERAL,
    AST_ERROR_EXPR,

    /* Statements */
    AST_VAR_DECL,
    AST_ASSIGN,
    AST_EXPR_STMT,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_BLOCK_STMT,

    /* Declarations */
    AST_FN_DECL,
    AST_STRUCT_DECL,
    AST_ENUM_DECL,
    AST_IMPORT_DECL
} NodeKind;

typedef struct AstExpr {
    NodeKind kind;
    SourcePos pos;
    Type *type; /* sema annotation */
} AstExpr;

typedef struct AstIntLiteral {
    AstExpr base;
    int64_t value;
    uint64_t uvalue;
} AstIntLiteral;

typedef struct AstFloatLiteral {
    AstExpr base;
    double value;
} AstFloatLiteral;

typedef struct AstStringLiteral {
    AstExpr base;
    const char *value;
    size_t length;
} AstStringLiteral;

typedef struct AstCharLiteral {
    AstExpr base;
    int64_t value;
} AstCharLiteral;

typedef struct AstBoolLiteral {
    AstExpr base;
    bool value;
} AstBoolLiteral;

typedef struct AstIdent {
    AstExpr base;
    const char *name;
    SemaSymbol *symbol;     /* sema annotation; NULL while parsing */
    bool refers_to_function;
    FunctionInfo *function; /* set when the name refers to a function */
} AstIdent;

typedef struct AstUnary {
    AstExpr base;
    UnOp op;
    AstExpr *operand;
} AstUnary;

typedef struct AstBinary {
    AstExpr base;
    BinOp op;
    AstExpr *lhs;
    AstExpr *rhs;
} AstBinary;

typedef struct AstCall {
    AstExpr base;
    AstExpr *callee;
    AstExpr **args;
    size_t argc;
    FunctionInfo *function; /* resolved direct callee */
    const BuiltinFn *builtin; /* resolved builtin */
    bool indirect;          /* call through a function value */
} AstCall;

typedef struct AstField {
    AstExpr base;
    AstExpr *object;
    const char *name;
    int field_index;          /* struct field index, -1 if not a struct field */
    bool is_module;           /* object is a module */
    const BuiltinFn *builtin;       /* module function */
    const BuiltinConst *constant;   /* module constant */
    EnumInfo *enum_info;      /* set for a qualified enum variant such as Color.Red */
    int variant_index;        /* index into enum_info->variants */
} AstField;

typedef struct AstIndex {
    AstExpr base;
    AstExpr *object;
    AstExpr *index;
} AstIndex;

typedef struct AstCast {
    AstExpr base;
    AstExpr *operand;
    AstType *target_type;
} AstCast;

typedef struct AstArrayLiteral {
    AstExpr base;
    AstExpr **elements;
    size_t count;
    bool is_repeat;
    int64_t repeat_count;
} AstArrayLiteral;

typedef struct AstStructLiteral {
    AstExpr base;
    AstType *type_syntax;
    const char **field_names;
    AstExpr **field_values;
    size_t count;
    StructInfo *info; /* sema annotation */
} AstStructLiteral;

/* ------------------------------------------------------------------ */
/* Statements                                                         */
/* ------------------------------------------------------------------ */

typedef struct AstStmt {
    NodeKind kind;
    SourcePos pos;
} AstStmt;

typedef struct AstBlock {
    SourcePos pos;
    AstStmt **stmts;
    size_t count;
    size_t capacity;
    Arena *arena;
} AstBlock;

typedef struct AstVarDecl {
    AstStmt base;
    bool is_mut;
    const char *name;
    AstType *type_syntax; /* may be NULL */
    AstExpr *init;        /* may be NULL */
    SemaSymbol *symbol;   /* sema annotation */
} AstVarDecl;

typedef struct AstAssign {
    AstStmt base;
    AstExpr *target;
    AstExpr *value;
    BinOp op; /* BIN_NONE for '=', otherwise compound assignment */
} AstAssign;

typedef struct AstExprStmt {
    AstStmt base;
    AstExpr *expr;
} AstExprStmt;

typedef struct AstIf {
    AstStmt base;
    AstExpr *cond;
    AstBlock *then_block;
    AstStmt *else_branch; /* AstIf (else if) or AstBlockStmt, may be NULL */
} AstIf;

typedef struct AstWhile {
    AstStmt base;
    AstExpr *cond;
    AstBlock *body;
} AstWhile;

typedef struct AstFor {
    AstStmt base;
    const char *var;
    SourcePos var_pos;
    AstExpr *start;
    AstExpr *end;
    bool inclusive;
    AstBlock *body;
    SemaSymbol *symbol; /* loop variable */
} AstFor;

typedef struct AstReturn {
    AstStmt base;
    AstExpr *value; /* may be NULL */
} AstReturn;

typedef struct AstBlockStmt {
    AstStmt base;
    AstBlock *block;
} AstBlockStmt;

/* ------------------------------------------------------------------ */
/* Declarations                                                       */
/* ------------------------------------------------------------------ */

typedef enum AstDeclKind {
    AST_IMPORT = AST_IMPORT_DECL,
    AST_FN = AST_FN_DECL,
    AST_STRUCT = AST_STRUCT_DECL,
    AST_ENUM = AST_ENUM_DECL
} AstDeclKind;

typedef struct AstDecl {
    NodeKind kind;
    SourcePos pos;
} AstDecl;

typedef struct AstParam {
    const char *name;
    AstType *type_syntax;
    SourcePos pos;
    SemaSymbol *symbol;
    Type *type;
} AstParam;

typedef struct AstFnDecl {
    AstDecl base;
    const char *name;
    AstParam *params;
    size_t nparams;
    AstType *return_type_syntax; /* NULL means void */
    AstBlock *body;              /* NULL for extern functions */
    bool is_extern;
    FunctionInfo *info;
} AstFnDecl;

typedef struct AstFieldDecl {
    const char *name;
    AstType *type_syntax;
    SourcePos pos;
} AstFieldDecl;

typedef struct AstStructDecl {
    AstDecl base;
    const char *name;
    AstFieldDecl *fields;
    size_t nfields;
    StructInfo *info;
} AstStructDecl;

typedef struct AstEnumVariant {
    const char *name;
    int64_t value;
    bool has_value;
    SourcePos pos;
} AstEnumVariant;

typedef struct AstEnumDecl {
    AstDecl base;
    const char *name;
    AstEnumVariant *variants;
    size_t nvariants;
    EnumInfo *info;
} AstEnumDecl;

typedef struct AstImportDecl {
    AstDecl base;
    const char *module;
} AstImportDecl;

RYN_DEFINE_VEC(AstExprVec, AstExpr *)
RYN_DEFINE_VEC(AstStmtVec, AstStmt *)
RYN_DEFINE_VEC(AstDeclVec, AstDecl *)

typedef struct AstProgram {
    Arena *arena;
    AstDeclVec decls;
} AstProgram;

/* ------------------------------------------------------------------ */
/* Constructors                                                       */
/* ------------------------------------------------------------------ */

AstProgram *ast_program_new(Arena *arena);
void ast_program_free(AstProgram *program);
AstType *ast_type_name(Arena *arena, SourcePos pos, const char *name);
AstType *ast_type_ptr(Arena *arena, SourcePos pos, AstType *elem);
AstType *ast_type_array(Arena *arena, SourcePos pos, AstType *elem, int64_t length);
AstType *ast_type_fn(Arena *arena, SourcePos pos, AstType **params, size_t nparams, AstType *ret);
AstType *ast_type_primitive(Arena *arena, SourcePos pos, const char *name);

AstExpr *ast_int_literal(Arena *arena, SourcePos pos, int64_t value, uint64_t uvalue);
AstExpr *ast_float_literal(Arena *arena, SourcePos pos, double value);
AstExpr *ast_string_literal(Arena *arena, SourcePos pos, const char *value, size_t length);
AstExpr *ast_char_literal(Arena *arena, SourcePos pos, int64_t value);
AstExpr *ast_bool_literal(Arena *arena, SourcePos pos, bool value);
AstExpr *ast_null_literal(Arena *arena, SourcePos pos);
AstExpr *ast_ident(Arena *arena, SourcePos pos, const char *name);
AstExpr *ast_unary(Arena *arena, SourcePos pos, UnOp op, AstExpr *operand);
AstExpr *ast_binary(Arena *arena, SourcePos pos, BinOp op, AstExpr *lhs, AstExpr *rhs);
AstExpr *ast_call(Arena *arena, SourcePos pos, AstExpr *callee, AstExpr **args, size_t argc);
AstExpr *ast_field(Arena *arena, SourcePos pos, AstExpr *object, const char *name);
AstExpr *ast_index(Arena *arena, SourcePos pos, AstExpr *object, AstExpr *index);
AstExpr *ast_cast(Arena *arena, SourcePos pos, AstExpr *operand, AstType *target);
AstExpr *ast_array_literal(Arena *arena, SourcePos pos, AstExpr **elements, size_t count,
                           bool is_repeat, int64_t repeat_count);
AstExpr *ast_error_expr(Arena *arena, SourcePos pos);
AstExpr *ast_struct_literal(Arena *arena, SourcePos pos, AstType *type_syntax, const char **names,
                            AstExpr **values, size_t count);

AstBlock *ast_block_new(Arena *arena, SourcePos pos);
void ast_block_add(AstBlock *block, AstStmt *stmt);

AstStmt *ast_var_decl(Arena *arena, SourcePos pos, bool is_mut, const char *name,
                      AstType *type_syntax, AstExpr *init);
AstStmt *ast_assign(Arena *arena, SourcePos pos, AstExpr *target, AstExpr *value, BinOp op);
AstStmt *ast_expr_stmt(Arena *arena, SourcePos pos, AstExpr *expr);
AstStmt *ast_if(Arena *arena, SourcePos pos, AstExpr *cond, AstBlock *then_block,
                AstStmt *else_branch);
AstStmt *ast_while(Arena *arena, SourcePos pos, AstExpr *cond, AstBlock *body);
AstStmt *ast_for(Arena *arena, SourcePos pos, const char *var, SourcePos var_pos, AstExpr *start,
                 AstExpr *end, bool inclusive, AstBlock *body);
AstStmt *ast_return(Arena *arena, SourcePos pos, AstExpr *value);
AstStmt *ast_break(Arena *arena, SourcePos pos);
AstStmt *ast_continue(Arena *arena, SourcePos pos);
AstStmt *ast_block_stmt(Arena *arena, SourcePos pos, AstBlock *block);

AstDecl *ast_fn_decl(Arena *arena, SourcePos pos, const char *name, AstParam *params,
                     size_t nparams, AstType *return_type, AstBlock *body, bool is_extern);
AstDecl *ast_struct_decl(Arena *arena, SourcePos pos, const char *name, AstFieldDecl *fields,
                         size_t nfields);
AstDecl *ast_enum_decl(Arena *arena, SourcePos pos, const char *name, AstEnumVariant *variants,
                       size_t nvariants);
AstDecl *ast_import_decl(Arena *arena, SourcePos pos, const char *module);
void ast_program_add(AstProgram *program, AstDecl *decl);

/* ------------------------------------------------------------------ */
/* Debug dumps                                                        */
/* ------------------------------------------------------------------ */

void ast_dump_program(const AstProgram *program, FILE *out);
void ast_dump_stmt(const AstStmt *stmt, FILE *out, int indent);
void ast_dump_expr(const AstExpr *expr, FILE *out);
void ast_dump_type_syntax(const AstType *type, FILE *out);

#endif /* RYN_AST_H */
