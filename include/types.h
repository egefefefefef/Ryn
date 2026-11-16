/*
 * Ryn programming language — type system.
 *
 * Types are immutable objects. Primitive types are canonical singletons owned
 * by a TypeContext; composite types (pointers, arrays, function types) are
 * allocated in the arena of the compilation. Struct and enum types wrap a
 * StructInfo / EnumInfo that the semantic analyzer fills in.
 *
 * Two types are equal when type_eq() returns true; for primitives and named
 * types this is pointer identity, composites are compared structurally.
 */
#ifndef RYN_TYPES_H
#define RYN_TYPES_H

#include "common.h"
#include "source.h"

typedef enum TypeKind {
    TY_VOID = 0,
    TY_BOOL,
    TY_CHAR,
    TY_STR,
    TY_I8,
    TY_I16,
    TY_I32,
    TY_I64,
    TY_U8,
    TY_U16,
    TY_U32,
    TY_U64,
    TY_F32,
    TY_F64,
    TY_PTR,
    TY_ARRAY,
    TY_STRUCT,
    TY_ENUM,
    TY_FN,

    /* Sema-internal types. They never reach the backend. */
    TY_NULL,        /* type of the 'null' literal, assignable to any pointer */
    TY_UNTYPED_INT, /* integer literal without an expected type yet */
    TY_UNTYPED_FLOAT,
    TY_ERROR        /* error recovery; absorbs all checks */
} TypeKind;

typedef struct Type Type;

typedef struct StructField {
    const char *name;
    Type *type;
    SourcePos pos;
} StructField;

typedef struct StructInfo {
    const char *name;
    StructField *fields;
    size_t nfields;
    bool resolved;
    bool resolving;
    SourcePos decl_pos;
    Type *type;    /* back reference, set by sema */
    void *ast_decl; /* AstStructDecl*, used by sema to resolve field syntax */
} StructInfo;

typedef struct EnumVariant {
    const char *name;
    int64_t value;
    SourcePos pos;
} EnumVariant;

typedef struct EnumInfo {
    const char *name;
    EnumVariant *variants;
    size_t nvariants;
    bool is_wide; /* true when a variant does not fit in i32; stored as i64 */
    SourcePos decl_pos;
    Type *type;
} EnumInfo;

struct Type {
    TypeKind kind;
    int id;
    union {
        struct {
            Type *elem;
        } ptr;
        struct {
            Type *elem;
            int64_t length;
        } array;
        StructInfo *strukt;
        EnumInfo *enumer;
        struct {
            Type **params;
            size_t nparams;
            Type *ret;
        } fn;
    } as;
};

typedef struct TypeContext {
    Arena *arena;
    Type *primitives[TY_ERROR + 1]; /* indexed by TypeKind */
    int next_id;
} TypeContext;

void types_init(TypeContext *ctx, Arena *arena);
Type *type_prim(TypeContext *ctx, TypeKind kind);
Type *type_ptr(Arena *arena, Type *elem);
Type *type_array(Arena *arena, Type *elem, int64_t length);
Type *type_fn(Arena *arena, Type **params, size_t nparams, Type *ret);
Type *type_new_struct(TypeContext *ctx, StructInfo *info);
Type *type_new_enum(TypeContext *ctx, EnumInfo *info);

bool type_is_error(const Type *type);
bool type_is_integer(const Type *type);
bool type_is_signed_int(const Type *type);
bool type_is_unsigned_int(const Type *type);
bool type_is_float(const Type *type);
bool type_is_numeric(const Type *type);
bool type_is_pointer(const Type *type);
bool type_is_aggregate(const Type *type);
bool type_is_scalar(const Type *type);
bool type_is_null(const Type *type);
bool type_is_untyped(const Type *type);

/* Numeric rank, used for implicit widening: i8 < i16 < i32 < i64. */
int type_int_rank(const Type *type);

/* True when 'from' can be implicitly converted to 'to' (numeric widening or
 * pointer-to-void). Literal adaptation is handled by the semantic analyzer. */
bool type_can_widen(const Type *from, const Type *to);

bool type_eq(const Type *a, const Type *b);

/* Human readable spelling, e.g. "ptr<i32>" or "[u8; 16]". */
const char *type_to_string(Arena *arena, const Type *type);
/* Writes into buf, used by diagnostics when no arena is at hand. */
void type_to_string_buf(const Type *type, char *buf, size_t buflen);
const char *type_kind_name(TypeKind kind);

#endif /* RYN_TYPES_H */
