#include "types.h"

static Type *make_type(Arena *arena, TypeKind kind, TypeContext *ctx) {
    Type *type = ARENA_ARRAY(arena, Type, 1);
    type->kind = kind;
    type->id = ctx ? ++ctx->next_id : 0;
    return type;
}

void types_init(TypeContext *ctx, Arena *arena) {
    ctx->arena = arena;
    ctx->next_id = 0;
    for (int kind = 0; kind <= (int)TY_F64; kind++) {
        Type *type = make_type(arena, (TypeKind)kind, ctx);
        ctx->primitives[kind] = type;
    }
    ctx->primitives[TY_NULL] = make_type(arena, TY_NULL, ctx);
    ctx->primitives[TY_UNTYPED_INT] = make_type(arena, TY_UNTYPED_INT, ctx);
    ctx->primitives[TY_UNTYPED_FLOAT] = make_type(arena, TY_UNTYPED_FLOAT, ctx);
    ctx->primitives[TY_ERROR] = make_type(arena, TY_ERROR, ctx);
}

Type *type_prim(TypeContext *ctx, TypeKind kind) {
    if ((int)kind < 0 || (int)kind > (int)TY_ERROR) {
        return ctx->primitives[TY_ERROR];
    }
    return ctx->primitives[kind];
}

Type *type_ptr(Arena *arena, Type *elem) {
    Type *type = make_type(arena, TY_PTR, NULL);
    type->as.ptr.elem = elem;
    return type;
}

Type *type_array(Arena *arena, Type *elem, int64_t length) {
    Type *type = make_type(arena, TY_ARRAY, NULL);
    type->as.array.elem = elem;
    type->as.array.length = length;
    return type;
}

Type *type_fn(Arena *arena, Type **params, size_t nparams, Type *ret) {
    Type *type = make_type(arena, TY_FN, NULL);
    type->as.fn.params = params;
    type->as.fn.nparams = nparams;
    type->as.fn.ret = ret;
    return type;
}

Type *type_new_struct(TypeContext *ctx, StructInfo *info) {
    Type *type = make_type(ctx->arena, TY_STRUCT, ctx);
    type->as.strukt = info;
    info->type = type;
    return type;
}

Type *type_new_enum(TypeContext *ctx, EnumInfo *info) {
    Type *type = make_type(ctx->arena, TY_ENUM, ctx);
    type->as.enumer = info;
    info->type = type;
    return type;
}

bool type_is_error(const Type *type) {
    return !type || type->kind == TY_ERROR;
}

bool type_is_integer(const Type *type) {
    return type && type->kind >= TY_I8 && type->kind <= TY_U64;
}

bool type_is_signed_int(const Type *type) {
    return type && type->kind >= TY_I8 && type->kind <= TY_I64;
}

bool type_is_unsigned_int(const Type *type) {
    return type && type->kind >= TY_U8 && type->kind <= TY_U64;
}

bool type_is_float(const Type *type) {
    return type && (type->kind == TY_F32 || type->kind == TY_F64);
}

bool type_is_numeric(const Type *type) {
    return type_is_integer(type) || type_is_float(type);
}

bool type_is_pointer(const Type *type) {
    return type && type->kind == TY_PTR;
}

bool type_is_aggregate(const Type *type) {
    return type && (type->kind == TY_ARRAY || type->kind == TY_STRUCT);
}

bool type_is_scalar(const Type *type) {
    if (!type) {
        return false;
    }
    switch (type->kind) {
    case TY_BOOL:
    case TY_CHAR:
    case TY_STR:
    case TY_I8:
    case TY_I16:
    case TY_I32:
    case TY_I64:
    case TY_U8:
    case TY_U16:
    case TY_U32:
    case TY_U64:
    case TY_F32:
    case TY_F64:
    case TY_PTR:
    case TY_ENUM:
    case TY_FN:
    case TY_NULL:
    case TY_UNTYPED_INT:
    case TY_UNTYPED_FLOAT:
        return true;
    default:
        return false;
    }
}

bool type_is_null(const Type *type) {
    return type && type->kind == TY_NULL;
}

bool type_is_untyped(const Type *type) {
    return type && (type->kind == TY_UNTYPED_INT || type->kind == TY_UNTYPED_FLOAT);
}

int type_int_rank(const Type *type) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
    case TY_I8:
    case TY_U8:
        return 1;
    case TY_I16:
    case TY_U16:
        return 2;
    case TY_I32:
    case TY_U32:
        return 3;
    case TY_I64:
    case TY_U64:
        return 4;
    default:
        return 0;
    }
}

bool type_can_widen(const Type *from, const Type *to) {
    if (!from || !to) {
        return false;
    }
    if (from->kind == TY_ERROR || to->kind == TY_ERROR) {
        return true;
    }
    if (type_eq(from, to)) {
        return true;
    }
    if (type_is_untyped(from)) {
        if (from->kind == TY_UNTYPED_INT) {
            return type_is_integer(to) || type_is_float(to);
        }
        return type_is_float(to);
    }
    if (to->kind == TY_PTR) {
        if (from->kind == TY_NULL) {
            return true;
        }
        if (from->kind == TY_PTR) {
            if (to->as.ptr.elem->kind == TY_VOID) {
                return true;
            }
            return type_eq(from->as.ptr.elem, to->as.ptr.elem);
        }
        return false;
    }
    if (type_is_integer(from) && type_is_integer(to)) {
        if (type_is_signed_int(from) != type_is_signed_int(to)) {
            return false;
        }
        return type_int_rank(from) <= type_int_rank(to);
    }
    return false;
}

bool type_eq(const Type *a, const Type *b) {
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
    case TY_PTR:
        return type_eq(a->as.ptr.elem, b->as.ptr.elem);
    case TY_ARRAY:
        return a->as.array.length == b->as.array.length &&
               type_eq(a->as.array.elem, b->as.array.elem);
    case TY_STRUCT:
    case TY_ENUM:
        /* Named types are canonical; equal kinds with different info are not equal. */
        return a->as.strukt == b->as.strukt;
    case TY_FN: {
        if (!type_eq(a->as.fn.ret, b->as.fn.ret) || a->as.fn.nparams != b->as.fn.nparams) {
            return false;
        }
        for (size_t i = 0; i < a->as.fn.nparams; i++) {
            if (!type_eq(a->as.fn.params[i], b->as.fn.params[i])) {
                return false;
            }
        }
        return true;
    }
    default:
        /* Primitives and internal types are canonical singletons. */
        return true;
    }
}

const char *type_kind_name(TypeKind kind) {
    switch (kind) {
    case TY_VOID: return "void";
    case TY_BOOL: return "bool";
    case TY_CHAR: return "char";
    case TY_STR: return "str";
    case TY_I8: return "i8";
    case TY_I16: return "i16";
    case TY_I32: return "i32";
    case TY_I64: return "i64";
    case TY_U8: return "u8";
    case TY_U16: return "u16";
    case TY_U32: return "u32";
    case TY_U64: return "u64";
    case TY_F32: return "f32";
    case TY_F64: return "f64";
    case TY_PTR: return "ptr";
    case TY_ARRAY: return "array";
    case TY_STRUCT: return "struct";
    case TY_ENUM: return "enum";
    case TY_FN: return "fn";
    case TY_NULL: return "null";
    case TY_UNTYPED_INT: return "integer literal";
    case TY_UNTYPED_FLOAT: return "float literal";
    case TY_ERROR: return "<error>";
    default: return "<unknown>";
    }
}

static void type_write(StrBuf *sb, const Type *type) {
    if (!type) {
        sb_append(sb, "<null>");
        return;
    }
    switch (type->kind) {
    case TY_PTR:
        sb_append(sb, "ptr<");
        type_write(sb, type->as.ptr.elem);
        sb_append(sb, ">");
        break;
    case TY_ARRAY:
        sb_append(sb, "[");
        type_write(sb, type->as.array.elem);
        sb_appendf(sb, "; %lld]", (long long)type->as.array.length);
        break;
    case TY_STRUCT:
        sb_append(sb, type->as.strukt->name);
        break;
    case TY_ENUM:
        sb_append(sb, type->as.enumer->name);
        break;
    case TY_FN:
        sb_append(sb, "fn(");
        for (size_t i = 0; i < type->as.fn.nparams; i++) {
            if (i) {
                sb_append(sb, ", ");
            }
            type_write(sb, type->as.fn.params[i]);
        }
        sb_append(sb, ")");
        if (type->as.fn.ret->kind != TY_VOID) {
            sb_append(sb, " -> ");
            type_write(sb, type->as.fn.ret);
        }
        break;
    default:
        sb_append(sb, type_kind_name(type->kind));
        break;
    }
}

const char *type_to_string(Arena *arena, const Type *type) {
    StrBuf sb;
    sb_init(&sb);
    type_write(&sb, type);
    char *result = arena_strdup(arena, sb.data ? sb.data : "");
    sb_free(&sb);
    return result;
}

void type_to_string_buf(const Type *type, char *buf, size_t buflen) {
    StrBuf sb;
    sb_init(&sb);
    type_write(&sb, type);
    snprintf(buf, buflen, "%s", sb.data ? sb.data : "");
    sb_free(&sb);
}
