/*
 * Ryn programming language — intermediate representation.
 *
 * The IR is a typed, linear instruction stream organised into basic blocks.
 * It is produced from the annotated AST and is the only input of a backend;
 * the C backend (and future LLVM backend) never looks at the AST.
 *
 * Design:
 *   - locals are named slots (params, Ryn variables and hidden temporaries);
 *     aggregates live in storage and are addressed, never copied implicitly;
 *   - every instruction that produces a value writes to a typed temporary;
 *   - control flow is explicit (conditional branch, jump, return);
 *   - function calls reference semantic functions/builtins, not C names.
 *
 * Expression statements are evaluated for their side effects. Aggregate
 * values (structs, arrays) are represented by the address of their storage.
 */
#ifndef RYN_IR_H
#define RYN_IR_H

#include "ast.h"
#include "diagnostics.h"
#include "types.h"

typedef enum IrOp {
    IR_CONST_I,     /* dst = integer literal             (ival/uval, type) */
    IR_CONST_F,     /* dst = float literal               (fval, type) */
    IR_CONST_STR,   /* dst = string literal              (string_id) */
    IR_CONST_NULL,  /* dst = null pointer                (type) */
    IR_FUNC_PTR,    /* dst = address of a function       (function) */
    IR_COPY,        /* dst = a */
    IR_BIN,         /* dst = a <binop> b                 (binop) */
    IR_CMP,         /* dst = (a <binop> b) as bool       (binop) */
    IR_STR_EQ,      /* dst = str content equality        (a, b) */
    IR_STR_INDEX,   /* dst = a[b] with bounds checking   (char result) */
    IR_UN,          /* dst = <unop> a                    (unop) */
    IR_CALL,        /* dst = function(args)              (function or builtin) */
    IR_ICALL,       /* dst = a(args)                     (a holds fn pointer) */
    IR_LOAD,        /* dst = *a */
    IR_STORE,       /* *a = b */
    IR_ADDR_LOCAL,  /* dst = &local                      (local) */
    IR_ADDR_TEMP,   /* dst = &temp                       (a) */
    IR_LOCAL_READ,  /* dst = local                       (local) */
    IR_LOCAL_WRITE, /* local = a                         (local) */
    IR_FIELD_ADDR,  /* dst = &a->field                   (a, field_index) */
    IR_ELEM_ADDR,   /* dst = &a[b]                       (a, b) */
    IR_CAST,        /* dst = (type)a */
    IR_AGG_COPY,    /* memcpy(a, b, sizeof aggregate)    (a, b) */
    IR_JMP,         /* goto a                            (a = block id) */
    IR_BR,          /* if (a) goto b else goto c         (a, b, c) */
    IR_RET,         /* return a (or nothing when a < 0) */
    IR_NOP
} IrOp;

typedef struct IrInstr {
    IrOp op;
    Type *type; /* result type (NULL when the instruction has no result) */
    int dst;    /* destination temporary, -1 for none */

    int a;
    int b;
    int c;

    int64_t ival;
    uint64_t uval;
    double fval;
    int string_id;
    int local;
    int field_index;
    int argc;
    int *args;

    BinOp binop;
    UnOp unop;

    const FunctionInfo *function;
    const BuiltinFn *builtin;

    struct IrInstr *next;
} IrInstr;

typedef struct IrBlock {
    int id;
    IrInstr *first;
    IrInstr *last;
} IrBlock;

typedef struct IrLocal {
    Type *type;
    const char *name; /* display name ("x", "tmp"); used in comments */
    bool is_param;
    bool is_hidden;
} IrLocal;

typedef struct IrTemp {
    Type *type;
} IrTemp;

typedef struct IrFunction {
    const char *name;
    const char *c_name;
    Type *return_type;
    IrLocal *locals;
    size_t nlocals;
    IrTemp *temps;
    size_t ntemps;
    IrBlock **blocks;
    size_t nblocks;
    int entry_block;
} IrFunction;

typedef struct IrString {
    const char *value;
    size_t length;
} IrString;

typedef struct IrModule {
    Arena *arena;
    IrFunction **functions;
    size_t nfunctions;
    IrString *strings;
    size_t nstrings;
} IrModule;

/* Builds the IR for all functions with a body. The program must have passed
 * semantic analysis. */
IrModule *ir_build(Arena *arena, Diagnostics *diag, AstProgram *program, TypeContext *types);
void ir_module_free(IrModule *module);

/* Human readable listing (used by --emit-ir and by tests). */
void ir_dump_module(const IrModule *module, FILE *out);
const char *ir_op_name(IrOp op);

/* Interning of string literals is exposed for backends that need to know the
 * final string table. */
const IrString *ir_string(const IrModule *module, int string_id);

#endif /* RYN_IR_H */
