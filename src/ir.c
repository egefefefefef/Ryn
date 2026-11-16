#include "ir.h"

#include "builtins.h"
#include "sema.h"

#include <inttypes.h>

/* ------------------------------------------------------------------ */
/* Builder                                                            */
/* ------------------------------------------------------------------ */

typedef struct LocalBinding {
    SemaSymbol *symbol;
    int local;
} LocalBinding;

typedef struct LoopTarget {
    int break_block;
    int continue_block;
} LoopTarget;

typedef struct Builder {
    Arena *arena;
    Diagnostics *diag;
    TypeContext *types;
    IrModule *module;
    IrFunction *fn;

    IrBlock *block;

    IrLocal *locals;
    size_t nlocals;
    size_t local_cap;
    IrTemp *temps;
    size_t ntemps;
    size_t temp_cap;
    IrBlock **blocks;
    size_t nblocks;
    size_t block_cap;

    LocalBinding *bindings;
    size_t nbindings;
    size_t binding_cap;

    LoopTarget loops[128];
    int loop_depth;

    bool failed;
} Builder;

static void *grow(void *items, size_t *capacity, size_t needed, size_t item_size) {
    if (needed <= *capacity) {
        return items;
    }
    size_t cap = *capacity ? *capacity * 2 : 8;
    while (cap < needed) {
        cap *= 2;
    }
    void *grown = realloc(items, cap * item_size);
    if (!grown) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    *capacity = cap;
    return grown;
}

static void internal_error(Builder *b, SourcePos pos, const char *fmt, ...) {
    if (b->failed) {
        return;
    }
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    diag_error_note(b->diag, pos, 1, E_UNSUPPORTED, "this is a compiler bug; please report it",
                    "internal IR error: %s", message);
    b->failed = true;
}

static IrBlock *new_block(Builder *b) {
    IrBlock *block = ARENA_ARRAY(b->arena, IrBlock, 1);
    block->id = (int)b->nblocks;
    b->blocks = (IrBlock **)grow(b->blocks, &b->block_cap, b->nblocks + 1, sizeof(IrBlock *));
    b->blocks[b->nblocks++] = block;
    return block;
}

static void set_block(Builder *b, IrBlock *block) {
    b->block = block;
}

static int new_temp(Builder *b, Type *type) {
    b->temps = (IrTemp *)grow(b->temps, &b->temp_cap, b->ntemps + 1, sizeof(IrTemp));
    b->temps[b->ntemps].type = type;
    return (int)b->ntemps++;
}

static int new_local(Builder *b, Type *type, const char *name, bool is_param, bool is_hidden) {
    b->locals = (IrLocal *)grow(b->locals, &b->local_cap, b->nlocals + 1, sizeof(IrLocal));
    b->locals[b->nlocals].type = type;
    b->locals[b->nlocals].name = name;
    b->locals[b->nlocals].is_param = is_param;
    b->locals[b->nlocals].is_hidden = is_hidden;
    return (int)b->nlocals++;
}

static IrInstr *emit(Builder *b, IrOp op) {
    IrInstr *instr = ARENA_ARRAY(b->arena, IrInstr, 1);
    instr->op = op;
    instr->dst = -1;
    instr->a = -1;
    instr->b = -1;
    instr->c = -1;
    instr->local = -1;
    instr->field_index = -1;
    instr->string_id = -1;
    instr->next = NULL;
    if (!b->block) {
        internal_error(b, (SourcePos){0, 1, 1, 0}, "instruction emitted outside a block");
        return instr;
    }
    if (b->block->last) {
        b->block->last->next = instr;
    } else {
        b->block->first = instr;
    }
    b->block->last = instr;
    return instr;
}

static bool block_terminated(const IrBlock *block) {
    if (!block || !block->last) {
        return false;
    }
    switch (block->last->op) {
    case IR_JMP:
    case IR_BR:
    case IR_RET:
        return true;
    default:
        return false;
    }
}

static void emit_jmp(Builder *b, IrBlock *target) {
    IrInstr *instr = emit(b, IR_JMP);
    instr->a = target->id;
}

static void emit_br(Builder *b, int cond, IrBlock *when_true, IrBlock *when_false) {
    IrInstr *instr = emit(b, IR_BR);
    instr->a = cond;
    instr->b = when_true->id;
    instr->c = when_false->id;
}

static void emit_ret(Builder *b, int value, Type *type) {
    IrInstr *instr = emit(b, IR_RET);
    instr->a = value;
    instr->type = type;
}

static int emit_const_int(Builder *b, Type *type, int64_t value, uint64_t uvalue) {
    int dst = new_temp(b, type);
    IrInstr *instr = emit(b, IR_CONST_I);
    instr->dst = dst;
    instr->type = type;
    instr->ival = value;
    instr->uval = uvalue;
    return dst;
}

static int emit_const_float(Builder *b, Type *type, double value) {
    int dst = new_temp(b, type);
    IrInstr *instr = emit(b, IR_CONST_F);
    instr->dst = dst;
    instr->type = type;
    instr->fval = value;
    return dst;
}

static int intern_string(Builder *b, const char *value, size_t length) {
    for (size_t i = 0; i < b->module->nstrings; i++) {
        if (b->module->strings[i].length == length &&
            memcmp(b->module->strings[i].value, value, length) == 0) {
            return (int)i;
        }
    }
    b->module->strings = (IrString *)realloc(
        b->module->strings, (b->module->nstrings + 1) * sizeof(IrString));
    if (!b->module->strings) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    b->module->strings[b->module->nstrings].value = value;
    b->module->strings[b->module->nstrings].length = length;
    return (int)b->module->nstrings++;
}

static int emit_string(Builder *b, const char *value, size_t length) {
    int id = intern_string(b, value, length);
    int dst = new_temp(b, type_prim(b->types, TY_STR));
    IrInstr *instr = emit(b, IR_CONST_STR);
    instr->dst = dst;
    instr->type = type_prim(b->types, TY_STR);
    instr->string_id = id;
    return dst;
}

static int emit_null(Builder *b) {
    Type *void_ptr = type_ptr(b->arena, type_prim(b->types, TY_VOID));
    int dst = new_temp(b, void_ptr);
    IrInstr *instr = emit(b, IR_CONST_NULL);
    instr->dst = dst;
    instr->type = void_ptr;
    return dst;
}

static int emit_cast(Builder *b, int value, Type *target) {
    if (!type_is_error(target) && type_eq(b->temps[value].type, target)) {
        return value;
    }
    int dst = new_temp(b, target);
    IrInstr *instr = emit(b, IR_CAST);
    instr->dst = dst;
    instr->a = value;
    instr->type = target;
    return dst;
}

static int bind_local(Builder *b, SemaSymbol *symbol) {
    for (size_t i = 0; i < b->nbindings; i++) {
        if (b->bindings[i].symbol == symbol) {
            return b->bindings[i].local;
        }
    }
    int local = new_local(b, symbol->type, symbol->name, symbol->is_param, false);
    b->bindings = (LocalBinding *)grow(b->bindings, &b->binding_cap, b->nbindings + 1,
                                      sizeof(LocalBinding));
    b->bindings[b->nbindings].symbol = symbol;
    b->bindings[b->nbindings].local = local;
    b->nbindings++;
    return local;
}

static int lookup_local(Builder *b, SemaSymbol *symbol) {
    for (size_t i = 0; i < b->nbindings; i++) {
        if (b->bindings[i].symbol == symbol) {
            return b->bindings[i].local;
        }
    }
    return bind_local(b, symbol);
}

static Type *pointer_to(Builder *b, Type *elem) {
    return type_ptr(b->arena, elem);
}

/* ------------------------------------------------------------------ */
/* Expression lowering                                                */
/* ------------------------------------------------------------------ */

static int build_expr(Builder *b, AstExpr *expr);

static int build_address(Builder *b, AstExpr *expr) {
    if (!expr) {
        internal_error(b, (SourcePos){0, 1, 1, 0}, "missing expression");
        return emit_null(b);
    }
    switch (expr->kind) {
    case AST_IDENT: {
        AstIdent *ident = (AstIdent *)expr;
        if (!ident->symbol) {
            internal_error(b, expr->pos, "identifier without a symbol");
            return emit_null(b);
        }
        int local = lookup_local(b, ident->symbol);
        int dst = new_temp(b, pointer_to(b, b->locals[local].type));
        IrInstr *instr = emit(b, IR_ADDR_LOCAL);
        instr->dst = dst;
        instr->local = local;
        instr->type = b->temps[dst].type;
        return dst;
    }
    case AST_FIELD: {
        AstField *field = (AstField *)expr;
        int base;
        if (field->object->type && field->object->type->kind == TY_PTR) {
            base = build_expr(b, field->object);
        } else {
            base = build_expr(b, field->object);
        }
        Type *base_type = b->temps[base].type;
        Type *struct_type = base_type;
        if (struct_type && struct_type->kind == TY_PTR) {
            struct_type = struct_type->as.ptr.elem;
        }
        if (!struct_type || struct_type->kind != TY_STRUCT) {
            internal_error(b, expr->pos, "field access on a non struct value");
            return emit_null(b);
        }
        StructInfo *info = struct_type->as.strukt;
        int index = field->field_index;
        if (index < 0 || (size_t)index >= info->nfields) {
            internal_error(b, expr->pos, "invalid field index");
            return emit_null(b);
        }
        int dst = new_temp(b, pointer_to(b, info->fields[index].type));
        IrInstr *instr = emit(b, IR_FIELD_ADDR);
        instr->dst = dst;
        instr->a = base;
        instr->field_index = index;
        instr->type = b->temps[dst].type;
        return dst;
    }
    case AST_INDEX: {
        AstIndex *index = (AstIndex *)expr;
        int base = build_expr(b, index->object);
        int idx = build_expr(b, index->index);
        Type *base_type = b->temps[base].type;
        Type *elem = NULL;
        if (base_type && base_type->kind == TY_PTR) {
            elem = base_type->as.ptr.elem;
            if (elem->kind == TY_ARRAY) {
                elem = elem->as.array.elem;
            }
        }
        if (!elem) {
            elem = index->base.type;
        }
        int dst = new_temp(b, pointer_to(b, elem));
        IrInstr *instr = emit(b, IR_ELEM_ADDR);
        instr->dst = dst;
        instr->a = base;
        instr->b = idx;
        instr->type = b->temps[dst].type;
        return dst;
    }
    case AST_UNARY: {
        AstUnary *unary = (AstUnary *)expr;
        if (unary->op == UN_DEREF) {
            return build_expr(b, unary->operand);
        }
        internal_error(b, expr->pos, "expression is not a valid address");
        return emit_null(b);
    }
    default:
        internal_error(b, expr->pos, "expression is not addressable");
        return emit_null(b);
    }
}

static int build_scalar_load(Builder *b, int address, Type *type) {
    int dst = new_temp(b, type);
    IrInstr *instr = emit(b, IR_LOAD);
    instr->dst = dst;
    instr->a = address;
    instr->type = type;
    return dst;
}

static void build_store_scalar(Builder *b, int address, int value) {
    IrInstr *instr = emit(b, IR_STORE);
    instr->a = address;
    instr->b = value;
}

static void build_agg_copy(Builder *b, int dst_address, int src_address) {
    IrInstr *instr = emit(b, IR_AGG_COPY);
    instr->a = dst_address;
    instr->b = src_address;
}

static int build_constant_int_expr(Builder *b, AstExpr *expr) {
    AstIntLiteral *lit = (AstIntLiteral *)expr;
    return emit_const_int(b, expr->type, lit->value, lit->uvalue);
}

static int build_struct_literal(Builder *b, AstStructLiteral *literal) {
    Type *type = literal->base.type;
    int local = new_local(b, type, "struct.tmp", false, true);
    for (size_t i = 0; i < literal->count; i++) {
        int base = new_temp(b, pointer_to(b, type));
        IrInstr *addr = emit(b, IR_ADDR_LOCAL);
        addr->dst = base;
        addr->local = local;
        addr->type = b->temps[base].type;

        const char *name = literal->field_names[i];
        int field_index = -1;
        for (size_t f = 0; f < literal->info->nfields; f++) {
            if (strcmp(literal->info->fields[f].name, name) == 0) {
                field_index = (int)f;
                break;
            }
        }
        if (field_index < 0) {
            internal_error(b, literal->base.pos, "unknown struct literal field");
            continue;
        }
        Type *field_type = literal->info->fields[field_index].type;
        int field_addr_temp = new_temp(b, pointer_to(b, field_type));
        IrInstr *field_instr = emit(b, IR_FIELD_ADDR);
        field_instr->dst = field_addr_temp;
        field_instr->a = base;
        field_instr->field_index = field_index;
        field_instr->type = b->temps[field_addr_temp].type;

        int value = build_expr(b, literal->field_values[i]);
        if (type_is_aggregate(field_type)) {
            build_agg_copy(b, field_addr_temp, value);
        } else {
            build_store_scalar(b, field_addr_temp, value);
        }
    }
    int dst = new_temp(b, pointer_to(b, type));
    IrInstr *instr = emit(b, IR_ADDR_LOCAL);
    instr->dst = dst;
    instr->local = local;
    instr->type = b->temps[dst].type;
    return dst;
}

static int build_array_literal(Builder *b, AstArrayLiteral *literal) {
    Type *type = literal->base.type;
    if (type->kind != TY_ARRAY) {
        internal_error(b, literal->base.pos, "array literal without an array type");
        return emit_null(b);
    }
    Type *elem = type->as.array.elem;
    int64_t count = type->as.array.length;
    int local = new_local(b, type, "array.tmp", false, true);

    int base = new_temp(b, pointer_to(b, type));
    IrInstr *addr = emit(b, IR_ADDR_LOCAL);
    addr->dst = base;
    addr->local = local;
    addr->type = b->temps[base].type;

    if (literal->is_repeat && count > 1) {
        int value = build_expr(b, literal->elements[0]);
        if (type_is_aggregate(elem)) {
            /* Build an IR loop copying the aggregate into each slot. */
            int index_local = new_local(b, type_prim(b->types, TY_I64), "fill.i", false, true);
            int init = emit_const_int(b, type_prim(b->types, TY_I64), 0, 0);
            IrInstr *write = emit(b, IR_LOCAL_WRITE);
            write->local = index_local;
            write->a = init;
            IrBlock *cond_block = new_block(b);
            IrBlock *body_block = new_block(b);
            IrBlock *end_block = new_block(b);
            emit_jmp(b, cond_block);
            set_block(b, cond_block);
            int idx = new_temp(b, type_prim(b->types, TY_I64));
            IrInstr *read = emit(b, IR_LOCAL_READ);
            read->dst = idx;
            read->local = index_local;
            read->type = type_prim(b->types, TY_I64);
            int limit = emit_const_int(b, type_prim(b->types, TY_I64), count, (uint64_t)count);
            int cond = new_temp(b, type_prim(b->types, TY_BOOL));
            IrInstr *cmp = emit(b, IR_CMP);
            cmp->dst = cond;
            cmp->a = idx;
            cmp->b = limit;
            cmp->binop = BIN_LT;
            cmp->type = type_prim(b->types, TY_BOOL);
            emit_br(b, cond, body_block, end_block);
            set_block(b, body_block);
            int slot_temp = new_temp(b, pointer_to(b, elem));
            IrInstr *slot = emit(b, IR_ELEM_ADDR);
            slot->dst = slot_temp;
            slot->a = base;
            slot->b = idx;
            slot->type = b->temps[slot_temp].type;
            build_agg_copy(b, slot_temp, value);
            int one = emit_const_int(b, type_prim(b->types, TY_I64), 1, 1);
            int next = new_temp(b, type_prim(b->types, TY_I64));
            IrInstr *inc = emit(b, IR_BIN);
            inc->dst = next;
            inc->a = idx;
            inc->b = one;
            inc->binop = BIN_ADD;
            inc->type = type_prim(b->types, TY_I64);
            IrInstr *update = emit(b, IR_LOCAL_WRITE);
            update->local = index_local;
            update->a = next;
            emit_jmp(b, cond_block);
            set_block(b, end_block);
        } else {
            for (int64_t i = 0; i < count; i++) {
                int idx = emit_const_int(b, type_prim(b->types, TY_I64), i, (uint64_t)i);
                int slot_temp = new_temp(b, pointer_to(b, elem));
                IrInstr *slot = emit(b, IR_ELEM_ADDR);
                slot->dst = slot_temp;
                slot->a = base;
                slot->b = idx;
                slot->type = b->temps[slot_temp].type;
                build_store_scalar(b, slot_temp, value);
            }
        }
    } else {
        size_t stored = literal->is_repeat ? (size_t)count : literal->count;
        for (size_t i = 0; i < stored; i++) {
            int idx = emit_const_int(b, type_prim(b->types, TY_I64), (int64_t)i, i);
            int slot_temp = new_temp(b, pointer_to(b, elem));
            IrInstr *slot = emit(b, IR_ELEM_ADDR);
            slot->dst = slot_temp;
            slot->a = base;
            slot->b = idx;
            slot->type = b->temps[slot_temp].type;
            int value = build_expr(b, literal->elements[i]);
            if (type_is_aggregate(elem)) {
                build_agg_copy(b, slot_temp, value);
            } else {
                build_store_scalar(b, slot_temp, value);
            }
        }
    }

    int dst = new_temp(b, pointer_to(b, type));
    IrInstr *result = emit(b, IR_ADDR_LOCAL);
    result->dst = dst;
    result->local = local;
    result->type = b->temps[dst].type;
    return dst;
}

static int build_logical(Builder *b, AstBinary *binary) {
    Type *bool_type = type_prim(b->types, TY_BOOL);
    int result_local = new_local(b, bool_type, "logic.tmp", false, true);
    int lhs = build_expr(b, binary->lhs);
    IrBlock *rhs_block = new_block(b);
    IrBlock *short_block = new_block(b);
    IrBlock *end_block = new_block(b);
    if (binary->op == BIN_LAND) {
        emit_br(b, lhs, rhs_block, short_block);
    } else {
        emit_br(b, lhs, short_block, rhs_block);
    }
    set_block(b, rhs_block);
    int rhs = build_expr(b, binary->rhs);
    IrInstr *write_rhs = emit(b, IR_LOCAL_WRITE);
    write_rhs->local = result_local;
    write_rhs->a = rhs;
    emit_jmp(b, end_block);

    set_block(b, short_block);
    int short_value = emit_const_int(b, bool_type, binary->op == BIN_LOR ? 1 : 0,
                                     binary->op == BIN_LOR ? 1 : 0);
    IrInstr *write_short = emit(b, IR_LOCAL_WRITE);
    write_short->local = result_local;
    write_short->a = short_value;
    emit_jmp(b, end_block);

    set_block(b, end_block);
    int dst = new_temp(b, bool_type);
    IrInstr *read = emit(b, IR_LOCAL_READ);
    read->dst = dst;
    read->local = result_local;
    read->type = bool_type;
    return dst;
}

static int build_call_expr(Builder *b, AstCall *call) {
    int callee_temp = -1;
    if (!call->builtin && !call->function) {
        if (!call->indirect) {
            internal_error(b, call->base.pos, "call without a resolved target");
            return emit_null(b);
        }
        callee_temp = build_expr(b, call->callee);
    }
    int *args = NULL;
    if (call->argc > 0) {
        args = ARENA_ARRAY(b->arena, int, call->argc);
        for (size_t i = 0; i < call->argc; i++) {
            args[i] = build_expr(b, call->args[i]);
        }
    }
    Type *result_type = call->base.type;
    bool is_void = result_type->kind == TY_VOID;
    int dst = is_void ? -1 : new_temp(b, result_type);
    IrInstr *instr;
    if (call->builtin) {
        instr = emit(b, IR_CALL);
        instr->builtin = call->builtin;
    } else if (call->indirect) {
        instr = emit(b, IR_ICALL);
        instr->a = callee_temp;
    } else {
        instr = emit(b, IR_CALL);
        instr->function = call->function;
    }
    instr->dst = dst;
    instr->type = result_type;
    instr->argc = (int)call->argc;
    instr->args = args;
    if (is_void) {
        return -1;
    }
    if (type_is_aggregate(result_type)) {
        int addr = new_temp(b, pointer_to(b, result_type));
        IrInstr *take = emit(b, IR_ADDR_TEMP);
        take->dst = addr;
        take->a = dst;
        take->type = b->temps[addr].type;
        return addr;
    }
    return dst;
}

static int build_expr(Builder *b, AstExpr *expr) {
    if (!expr) {
        internal_error(b, (SourcePos){0, 1, 1, 0}, "missing expression");
        return emit_null(b);
    }
    switch (expr->kind) {
    case AST_INT_LITERAL:
        return build_constant_int_expr(b, expr);
    case AST_FLOAT_LITERAL:
        return emit_const_float(b, expr->type, ((AstFloatLiteral *)expr)->value);
    case AST_STRING_LITERAL: {
        AstStringLiteral *str = (AstStringLiteral *)expr;
        return emit_string(b, str->value, str->length);
    }
    case AST_CHAR_LITERAL:
        return emit_const_int(b, expr->type, ((AstCharLiteral *)expr)->value,
                              (uint64_t)((AstCharLiteral *)expr)->value);
    case AST_BOOL_LITERAL:
        return emit_const_int(b, type_prim(b->types, TY_BOOL),
                              ((AstBoolLiteral *)expr)->value ? 1 : 0,
                              ((AstBoolLiteral *)expr)->value ? 1u : 0u);
    case AST_NULL_LITERAL:
        return emit_null(b);
    case AST_IDENT: {
        AstIdent *ident = (AstIdent *)expr;
        if (ident->refers_to_function && ident->function) {
            int dst = new_temp(b, ident->function->fn_type);
            IrInstr *instr = emit(b, IR_FUNC_PTR);
            instr->dst = dst;
            instr->function = ident->function;
            instr->type = ident->function->fn_type;
            return dst;
        }
        if (!ident->symbol) {
            internal_error(b, expr->pos, "variable without a symbol");
            return emit_null(b);
        }
        int local = lookup_local(b, ident->symbol);
        Type *type = b->locals[local].type;
        if (type_is_aggregate(type)) {
            int dst = new_temp(b, pointer_to(b, type));
            IrInstr *instr = emit(b, IR_ADDR_LOCAL);
            instr->dst = dst;
            instr->local = local;
            instr->type = b->temps[dst].type;
            return dst;
        }
        int dst = new_temp(b, type);
        IrInstr *instr = emit(b, IR_LOCAL_READ);
        instr->dst = dst;
        instr->local = local;
        instr->type = type;
        return dst;
    }
    case AST_UNARY: {
        AstUnary *unary = (AstUnary *)expr;
        switch (unary->op) {
        case UN_ADDR:
            return build_address(b, unary->operand);
        case UN_DEREF: {
            int pointer = build_expr(b, unary->operand);
            if (type_is_aggregate(expr->type)) {
                return pointer;
            }
            return build_scalar_load(b, pointer, expr->type);
        }
        case UN_NEG:
            if (unary->operand->kind == AST_INT_LITERAL) {
                AstIntLiteral *lit = (AstIntLiteral *)unary->operand;
                uint64_t negated = (uint64_t)0 - lit->uvalue;
                return emit_const_int(b, expr->type, (int64_t)negated, negated);
            }
            if (unary->operand->kind == AST_FLOAT_LITERAL) {
                return emit_const_float(b, expr->type,
                                        -((AstFloatLiteral *)unary->operand)->value);
            }
            break;
        default:
            break;
        }
        int operand = build_expr(b, unary->operand);
        int dst = new_temp(b, expr->type);
        IrInstr *instr = emit(b, IR_UN);
        instr->dst = dst;
        instr->a = operand;
        instr->unop = unary->op;
        instr->type = expr->type;
        return dst;
    }
    case AST_BINARY: {
        AstBinary *binary = (AstBinary *)expr;
        if (binop_is_logical(binary->op)) {
            return build_logical(b, binary);
        }
        int lhs = build_expr(b, binary->lhs);
        int rhs = build_expr(b, binary->rhs);
        if ((binary->op == BIN_EQ || binary->op == BIN_NE) &&
            (binary->lhs->type->kind == TY_STR || binary->rhs->type->kind == TY_STR)) {
            int dst = new_temp(b, type_prim(b->types, TY_BOOL));
            IrInstr *instr = emit(b, IR_STR_EQ);
            instr->dst = dst;
            instr->a = lhs;
            instr->b = rhs;
            instr->type = type_prim(b->types, TY_BOOL);
            if (binary->op == BIN_NE) {
                int negated = new_temp(b, type_prim(b->types, TY_BOOL));
                IrInstr *not_instr = emit(b, IR_UN);
                not_instr->dst = negated;
                not_instr->a = dst;
                not_instr->unop = UN_NOT;
                not_instr->type = type_prim(b->types, TY_BOOL);
                return negated;
            }
            return dst;
        }
        if (binop_is_comparison(binary->op)) {
            int dst = new_temp(b, type_prim(b->types, TY_BOOL));
            IrInstr *instr = emit(b, IR_CMP);
            instr->dst = dst;
            instr->a = lhs;
            instr->b = rhs;
            instr->binop = binary->op;
            instr->type = type_prim(b->types, TY_BOOL);
            return dst;
        }
        int dst = new_temp(b, expr->type);
        IrInstr *instr = emit(b, IR_BIN);
        instr->dst = dst;
        instr->a = lhs;
        instr->b = rhs;
        instr->binop = binary->op;
        instr->type = expr->type;
        return dst;
    }
    case AST_CALL:
        return build_call_expr(b, (AstCall *)expr);
    case AST_FIELD: {
        AstField *field = (AstField *)expr;
        if (field->constant) {
            Type *type = field->base.type;
            if (field->constant->is_integer) {
                return emit_const_int(b, type, field->constant->ivalue,
                                      (uint64_t)field->constant->ivalue);
            }
            return emit_const_float(b, type, field->constant->fvalue);
        }
        if (field->enum_info) {
            int64_t value = field->enum_info->variants[field->variant_index].value;
            return emit_const_int(b, expr->type, value, (uint64_t)value);
        }
        int address = build_address(b, expr);
        if (type_is_aggregate(expr->type)) {
            return address;
        }
        return build_scalar_load(b, address, expr->type);
    }
    case AST_INDEX: {
        AstIndex *index = (AstIndex *)expr;
        int base_type_kind = index->object->type->kind;
        if (base_type_kind == TY_STR) {
            int base = build_expr(b, index->object);
            int idx = build_expr(b, index->index);
            int dst = new_temp(b, type_prim(b->types, TY_CHAR));
            IrInstr *instr = emit(b, IR_STR_INDEX);
            instr->dst = dst;
            instr->a = base;
            instr->b = idx;
            instr->type = type_prim(b->types, TY_CHAR);
            return dst;
        }
        int address = build_address(b, expr);
        if (type_is_aggregate(expr->type)) {
            return address;
        }
        return build_scalar_load(b, address, expr->type);
    }
    case AST_CAST: {
        AstCast *cast = (AstCast *)expr;
        if (cast->operand->kind == AST_NULL_LITERAL) {
            int null_temp = emit_null(b);
            return emit_cast(b, null_temp, expr->type);
        }
        int value = build_expr(b, cast->operand);
        return emit_cast(b, value, expr->type);
    }
    case AST_STRUCT_LITERAL:
        return build_struct_literal(b, (AstStructLiteral *)expr);
    case AST_ARRAY_LITERAL:
        return build_array_literal(b, (AstArrayLiteral *)expr);
    default:
        internal_error(b, expr->pos, "unsupported expression in IR builder");
        return emit_null(b);
    }
}

/* ------------------------------------------------------------------ */
/* Statement lowering                                                 */
/* ------------------------------------------------------------------ */

static void build_block(Builder *b, AstBlock *block);

static void build_var_decl(Builder *b, AstVarDecl *decl) {
    int local = bind_local(b, decl->symbol);
    if (!decl->init) {
        return;
    }
    Type *type = b->locals[local].type;
    if (type_is_aggregate(type)) {
        int address = new_temp(b, pointer_to(b, type));
        IrInstr *addr = emit(b, IR_ADDR_LOCAL);
        addr->dst = address;
        addr->local = local;
        addr->type = b->temps[address].type;
        int value = build_expr(b, decl->init);
        build_agg_copy(b, address, value);
    } else {
        int value = build_expr(b, decl->init);
        IrInstr *write = emit(b, IR_LOCAL_WRITE);
        write->local = local;
        write->a = value;
    }
}

static void build_assign(Builder *b, AstAssign *assign) {
    Type *target_type = assign->target->type;
    if (assign->op == BIN_NONE) {
        if (type_is_aggregate(target_type)) {
            int dst_address = build_address(b, assign->target);
            int src_address = build_expr(b, assign->value);
            build_agg_copy(b, dst_address, src_address);
        } else {
            int value = build_expr(b, assign->value);
            int address = build_address(b, assign->target);
            build_store_scalar(b, address, value);
        }
        return;
    }
    int address = build_address(b, assign->target);
    int target_value = build_scalar_load(b, address, target_type);
    int value = build_expr(b, assign->value);
    int result = new_temp(b, target_type);
    IrInstr *instr = emit(b, IR_BIN);
    instr->dst = result;
    instr->a = target_value;
    instr->b = value;
    instr->binop = assign->op;
    instr->type = target_type;
    build_store_scalar(b, address, result);
}

static void build_if_chain(Builder *b, AstStmt *stmt, IrBlock *end_block) {
    AstIf *if_stmt = (AstIf *)stmt;
    int cond = build_expr(b, if_stmt->cond);
    IrBlock *then_block = new_block(b);
    IrBlock *else_block = if_stmt->else_branch ? new_block(b) : NULL;
    emit_br(b, cond, then_block, else_block ? else_block : end_block);

    set_block(b, then_block);
    build_block(b, if_stmt->then_block);
    if (!block_terminated(b->block)) {
        emit_jmp(b, end_block);
    }

    if (!else_block) {
        return;
    }
    set_block(b, else_block);
    if (if_stmt->else_branch->kind == AST_IF) {
        build_if_chain(b, if_stmt->else_branch, end_block);
    } else {
        build_block(b, ((AstBlockStmt *)if_stmt->else_branch)->block);
        if (!block_terminated(b->block)) {
            emit_jmp(b, end_block);
        }
    }
}

static void build_if(Builder *b, AstIf *if_stmt) {
    IrBlock *end_block = new_block(b);
    build_if_chain(b, (AstStmt *)if_stmt, end_block);
    set_block(b, end_block);
}

static void build_while(Builder *b, AstWhile *while_stmt) {
    IrBlock *cond_block = new_block(b);
    IrBlock *body_block = new_block(b);
    IrBlock *end_block = new_block(b);
    emit_jmp(b, cond_block);
    set_block(b, cond_block);
    int cond = build_expr(b, while_stmt->cond);
    emit_br(b, cond, body_block, end_block);
    set_block(b, body_block);
    if (b->loop_depth >= 128) {
        internal_error(b, while_stmt->base.pos, "loop nesting too deep");
        return;
    }
    b->loops[b->loop_depth].break_block = end_block->id;
    b->loops[b->loop_depth].continue_block = cond_block->id;
    b->loop_depth++;
    build_block(b, while_stmt->body);
    b->loop_depth--;
    if (!block_terminated(b->block)) {
        emit_jmp(b, cond_block);
    }
    set_block(b, end_block);
}

static void build_for(Builder *b, AstFor *for_stmt) {
    if (!for_stmt->symbol) {
        internal_error(b, for_stmt->base.pos, "for loop without a loop variable");
        return;
    }
    int loop_local = bind_local(b, for_stmt->symbol);
    Type *loop_type = b->locals[loop_local].type;
    int start = build_expr(b, for_stmt->start);
    int end = build_expr(b, for_stmt->end);

    IrInstr *init_write = emit(b, IR_LOCAL_WRITE);
    init_write->local = loop_local;
    init_write->a = start;

    IrBlock *cond_block = new_block(b);
    IrBlock *body_block = new_block(b);
    IrBlock *incr_block = new_block(b);
    IrBlock *end_block = new_block(b);
    emit_jmp(b, cond_block);

    set_block(b, cond_block);
    int current = new_temp(b, loop_type);
    IrInstr *read = emit(b, IR_LOCAL_READ);
    read->dst = current;
    read->local = loop_local;
    read->type = loop_type;
    int condition = new_temp(b, type_prim(b->types, TY_BOOL));
    IrInstr *compare = emit(b, IR_CMP);
    compare->dst = condition;
    compare->a = current;
    compare->b = end;
    compare->binop = for_stmt->inclusive ? BIN_LE : BIN_LT;
    compare->type = type_prim(b->types, TY_BOOL);
    emit_br(b, condition, body_block, end_block);

    set_block(b, body_block);
    b->loops[b->loop_depth].break_block = end_block->id;
    b->loops[b->loop_depth].continue_block = incr_block->id;
    b->loop_depth++;
    build_block(b, for_stmt->body);
    b->loop_depth--;
    if (!block_terminated(b->block)) {
        emit_jmp(b, incr_block);
    }

    set_block(b, incr_block);
    int value = new_temp(b, loop_type);
    IrInstr *read2 = emit(b, IR_LOCAL_READ);
    read2->dst = value;
    read2->local = loop_local;
    read2->type = loop_type;
    int one = emit_const_int(b, loop_type, 1, 1);
    int next = new_temp(b, loop_type);
    IrInstr *add = emit(b, IR_BIN);
    add->dst = next;
    add->a = value;
    add->b = one;
    add->binop = BIN_ADD;
    add->type = loop_type;
    IrInstr *write = emit(b, IR_LOCAL_WRITE);
    write->local = loop_local;
    write->a = next;
    emit_jmp(b, cond_block);

    set_block(b, end_block);
}

static void build_return(Builder *b, AstReturn *ret) {
    if (!ret->value) {
        emit_ret(b, -1, NULL);
        return;
    }
    bool aggregate = type_is_aggregate(ret->value->type);
    int value = build_expr(b, ret->value);
    if (aggregate) {
        /* The backend dereferences the address to return the struct by value. */
        emit_ret(b, value, ret->value->type);
    } else {
        emit_ret(b, value, ret->value->type);
    }
}

static void build_stmt(Builder *b, AstStmt *stmt) {
    if (!stmt) {
        return;
    }
    switch (stmt->kind) {
    case AST_VAR_DECL:
        build_var_decl(b, (AstVarDecl *)stmt);
        break;
    case AST_ASSIGN:
        build_assign(b, (AstAssign *)stmt);
        break;
    case AST_EXPR_STMT: {
        AstExpr *expr = ((AstExprStmt *)stmt)->expr;
        if (expr->type->kind == TY_VOID) {
            build_expr(b, expr);
        } else {
            (void)build_expr(b, expr);
        }
        break;
    }
    case AST_IF:
        build_if(b, (AstIf *)stmt);
        break;
    case AST_WHILE:
        build_while(b, (AstWhile *)stmt);
        break;
    case AST_FOR:
        build_for(b, (AstFor *)stmt);
        break;
    case AST_RETURN:
        build_return(b, (AstReturn *)stmt);
        break;
    case AST_BREAK: {
        if (b->loop_depth == 0) {
            internal_error(b, stmt->pos, "break outside a loop");
            break;
        }
        IrBlock *target = b->blocks[b->loops[b->loop_depth - 1].break_block];
        emit_jmp(b, target);
        break;
    }
    case AST_CONTINUE: {
        if (b->loop_depth == 0) {
            internal_error(b, stmt->pos, "continue outside a loop");
            break;
        }
        IrBlock *target = b->blocks[b->loops[b->loop_depth - 1].continue_block];
        emit_jmp(b, target);
        break;
    }
    case AST_BLOCK_STMT:
        build_block(b, ((AstBlockStmt *)stmt)->block);
        break;
    default:
        internal_error(b, stmt->pos, "unsupported statement");
        break;
    }
}

static void build_block(Builder *b, AstBlock *block) {
    if (!block) {
        return;
    }
    for (size_t i = 0; i < block->count; i++) {
        build_stmt(b, block->stmts[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Module construction                                                */
/* ------------------------------------------------------------------ */

static void finish_function(Builder *b) {
    if (!block_terminated(b->block)) {
        emit_ret(b, -1, NULL);
    }
    IrFunction *fn = b->fn;
    fn->locals = ARENA_ARRAY(b->arena, IrLocal, b->nlocals);
    if (b->nlocals) {
        memcpy(fn->locals, b->locals, b->nlocals * sizeof(IrLocal));
    }
    fn->nlocals = b->nlocals;
    fn->temps = ARENA_ARRAY(b->arena, IrTemp, b->ntemps);
    if (b->ntemps) {
        memcpy(fn->temps, b->temps, b->ntemps * sizeof(IrTemp));
    }
    fn->ntemps = b->ntemps;
    fn->blocks = ARENA_ARRAY(b->arena, IrBlock *, b->nblocks);
    if (b->nblocks) {
        memcpy(fn->blocks, b->blocks, b->nblocks * sizeof(IrBlock *));
    }
    fn->nblocks = b->nblocks;
    fn->entry_block = 0;
}

IrModule *ir_build(Arena *arena, Diagnostics *diag, AstProgram *program, TypeContext *types) {
    IrModule *module = ARENA_ARRAY(arena, IrModule, 1);
    module->arena = arena;
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind != AST_FN_DECL) {
            continue;
        }
        AstFnDecl *fn_decl = (AstFnDecl *)decl;
        if (fn_decl->is_extern || !fn_decl->body) {
            continue;
        }
        Builder builder;
        memset(&builder, 0, sizeof(builder));
        builder.arena = arena;
        builder.diag = diag;
        builder.types = types;
        builder.module = module;

        IrFunction *fn = ARENA_ARRAY(arena, IrFunction, 1);
        fn->name = fn_decl->name;
        fn->c_name = fn_decl->info->c_name;
        fn->return_type = fn_decl->info->return_type;
        builder.fn = fn;

        for (size_t p = 0; p < fn_decl->nparams; p++) {
            if (fn_decl->params[p].symbol) {
                int local = new_local(&builder, fn_decl->params[p].type,
                                      fn_decl->params[p].name, true, false);
                builder.bindings =
                    (LocalBinding *)grow(builder.bindings, &builder.binding_cap,
                                         builder.nbindings + 1, sizeof(LocalBinding));
                builder.bindings[builder.nbindings].symbol = fn_decl->params[p].symbol;
                builder.bindings[builder.nbindings].local = local;
                builder.nbindings++;
            }
        }

        IrBlock *entry = new_block(&builder);
        builder.block = entry;
        build_block(&builder, fn_decl->body);
        finish_function(&builder);

        module->functions = (IrFunction **)realloc(
            module->functions, (module->nfunctions + 1) * sizeof(IrFunction *));
        if (!module->functions) {
            fprintf(stderr, "ryn: out of memory\n");
            abort();
        }
        module->functions[module->nfunctions++] = fn;

        free(builder.locals);
        free(builder.temps);
        free(builder.blocks);
        free(builder.bindings);
    }
    return module;
}

void ir_module_free(IrModule *module) {
    if (!module) {
        return;
    }
    free(module->functions);
    free(module->strings);
    module->functions = NULL;
    module->nfunctions = 0;
    module->strings = NULL;
    module->nstrings = 0;
}

const IrString *ir_string(const IrModule *module, int string_id) {
    if (!module || string_id < 0 || (size_t)string_id >= module->nstrings) {
        return NULL;
    }
    return &module->strings[string_id];
}

/* ------------------------------------------------------------------ */
/* Dump                                                               */
/* ------------------------------------------------------------------ */

const char *ir_op_name(IrOp op) {
    switch (op) {
    case IR_CONST_I: return "const.i";
    case IR_CONST_F: return "const.f";
    case IR_CONST_STR: return "const.str";
    case IR_CONST_NULL: return "const.null";
    case IR_FUNC_PTR: return "func.ptr";
    case IR_COPY: return "copy";
    case IR_BIN: return "bin";
    case IR_CMP: return "cmp";
    case IR_STR_EQ: return "str.eq";
    case IR_STR_INDEX: return "str.index";
    case IR_UN: return "un";
    case IR_CALL: return "call";
    case IR_ICALL: return "icall";
    case IR_LOAD: return "load";
    case IR_STORE: return "store";
    case IR_ADDR_LOCAL: return "addr.local";
    case IR_ADDR_TEMP: return "addr.temp";
    case IR_LOCAL_READ: return "local.read";
    case IR_LOCAL_WRITE: return "local.write";
    case IR_FIELD_ADDR: return "field.addr";
    case IR_ELEM_ADDR: return "elem.addr";
    case IR_CAST: return "cast";
    case IR_AGG_COPY: return "agg.copy";
    case IR_JMP: return "jmp";
    case IR_BR: return "br";
    case IR_RET: return "ret";
    case IR_NOP: return "nop";
    default: return "?";
    }
}

static void dump_temp(const IrFunction *fn, FILE *out, int index) {
    if (index < 0) {
        fputs("-", out);
        return;
    }
    fprintf(out, "%%%d", index);
    if ((size_t)index < fn->ntemps) {
        char type_buf[96];
        type_to_string_buf(fn->temps[index].type, type_buf, sizeof(type_buf));
        fprintf(out, ":%s", type_buf);
    }
}

static void dump_instr(const IrFunction *fn, const IrInstr *instr, FILE *out) {
    fputs("    ", out);
    if (instr->dst >= 0) {
        dump_temp(fn, out, instr->dst);
        fputs(" = ", out);
    }
    fprintf(out, "%s", ir_op_name(instr->op));
    switch (instr->op) {
    case IR_CONST_I:
        fprintf(out, " %" PRId64, instr->ival);
        break;
    case IR_CONST_F:
        fprintf(out, " %g", instr->fval);
        break;
    case IR_CONST_STR:
        fprintf(out, " #%d", instr->string_id);
        break;
    case IR_CONST_NULL:
    case IR_NOP:
        break;
    case IR_FUNC_PTR:
        if (instr->function) {
            fprintf(out, " @%s", instr->function->c_name);
        }
        break;
    case IR_COPY:
        fputc(' ', out);
        dump_temp(fn, out, instr->a);
        break;
    case IR_BIN:
    case IR_CMP:
        fprintf(out, " %s ", binop_name(instr->binop));
        dump_temp(fn, out, instr->a);
        fputc(' ', out);
        dump_temp(fn, out, instr->b);
        break;
    case IR_STR_EQ:
        fputc(' ', out);
        dump_temp(fn, out, instr->a);
        fputc(' ', out);
        dump_temp(fn, out, instr->b);
        break;
    case IR_STR_INDEX:
        fputs(" str[", out);
        dump_temp(fn, out, instr->a);
        fputc(',', out);
        dump_temp(fn, out, instr->b);
        fputc(']', out);
        break;
    case IR_UN:
        fprintf(out, " %s ", unop_name(instr->unop));
        dump_temp(fn, out, instr->a);
        break;
    case IR_CALL:
        if (instr->function) {
            fprintf(out, " @%s", instr->function->c_name);
        } else if (instr->builtin) {
            fprintf(out, " %s", instr->builtin->c_name);
        }
        fputc('(', out);
        for (int i = 0; i < instr->argc; i++) {
            if (i) {
                fputs(", ", out);
            }
            dump_temp(fn, out, instr->args[i]);
        }
        fputc(')', out);
        break;
    case IR_ICALL:
        fputs(" ", out);
        dump_temp(fn, out, instr->a);
        fputc('(', out);
        for (int i = 0; i < instr->argc; i++) {
            if (i) {
                fputs(", ", out);
            }
            dump_temp(fn, out, instr->args[i]);
        }
        fputc(')', out);
        break;
    case IR_LOAD:
        fputs(" *", out);
        dump_temp(fn, out, instr->a);
        break;
    case IR_STORE:
        fputs(" *", out);
        dump_temp(fn, out, instr->a);
        fputs(" = ", out);
        dump_temp(fn, out, instr->b);
        break;
    case IR_ADDR_TEMP:
        fputs(" &", out);
        dump_temp(fn, out, instr->a);
        break;
    case IR_ADDR_LOCAL:
    case IR_LOCAL_READ:
        fprintf(out, " l%d", instr->local);
        if ((size_t)instr->local < fn->nlocals) {
            fprintf(out, "(%s)", fn->locals[instr->local].name ? fn->locals[instr->local].name
                                                               : "?");
        }
        break;
    case IR_LOCAL_WRITE:
        fprintf(out, " l%d <- ", instr->local);
        dump_temp(fn, out, instr->a);
        break;
    case IR_FIELD_ADDR:
        fputs(" ", out);
        dump_temp(fn, out, instr->a);
        fprintf(out, " .#%d", instr->field_index);
        break;
    case IR_ELEM_ADDR:
        fputs(" &", out);
        dump_temp(fn, out, instr->a);
        fputc('[', out);
        dump_temp(fn, out, instr->b);
        fputc(']', out);
        break;
    case IR_CAST:
        fputs(" ", out);
        dump_temp(fn, out, instr->a);
        break;
    case IR_AGG_COPY:
        fputs(" ", out);
        dump_temp(fn, out, instr->b);
        fputs(" -> ", out);
        dump_temp(fn, out, instr->a);
        break;
    case IR_JMP:
        fprintf(out, " bb%d", instr->a);
        break;
    case IR_BR:
        fputs(" ", out);
        dump_temp(fn, out, instr->a);
        fprintf(out, " ? bb%d : bb%d", instr->b, instr->c);
        break;
    case IR_RET:
        if (instr->a >= 0) {
            fputc(' ', out);
            dump_temp(fn, out, instr->a);
        }
        break;
    default:
        break;
    }
    fputc('\n', out);
}

void ir_dump_module(const IrModule *module, FILE *out) {
    if (!module) {
        return;
    }
    fprintf(out, "; Ryn IR — %zu function%s, %zu string%s\n", module->nfunctions,
            module->nfunctions == 1 ? "" : "s", module->nstrings,
            module->nstrings == 1 ? "" : "s");
    for (size_t s = 0; s < module->nstrings; s++) {
        fprintf(out, "; string #%zu = \"%.*s\"\n", s, (int)module->strings[s].length,
                module->strings[s].value);
    }
    for (size_t f = 0; f < module->nfunctions; f++) {
        const IrFunction *fn = module->functions[f];
        char ret_buf[96];
        type_to_string_buf(fn->return_type, ret_buf, sizeof(ret_buf));
        fprintf(out, "\nfn %s() -> %s {\n", fn->c_name, ret_buf);
        for (size_t l = 0; l < fn->nlocals; l++) {
            char type_buf[96];
            type_to_string_buf(fn->locals[l].type, type_buf, sizeof(type_buf));
            fprintf(out, "  local l%zu %s %s%s\n", l, type_buf,
                    fn->locals[l].name ? fn->locals[l].name : "?",
                    fn->locals[l].is_param ? " (param)" : "");
        }
        for (size_t b = 0; b < fn->nblocks; b++) {
            const IrBlock *block = fn->blocks[b];
            fprintf(out, "  bb%d:\n", block->id);
            for (const IrInstr *instr = block->first; instr; instr = instr->next) {
                dump_instr(fn, instr, out);
            }
        }
        fputs("}\n", out);
    }
}
