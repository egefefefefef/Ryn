#include "parser.h"

RYN_DEFINE_VEC(AstParamVec, AstParam)
RYN_DEFINE_VEC(AstFieldDeclVec, AstFieldDecl)
RYN_DEFINE_VEC(AstEnumVariantVec, AstEnumVariant)
RYN_DEFINE_VEC(AstExprPtrVec, AstExpr *)

/* Vector of C strings used while parsing struct literals. */
typedef struct AstStringVec {
    const char **items;
    size_t len;
    size_t cap;
} AstStringVec;

static void ast_string_vec_init(AstStringVec *v) {
    v->items = NULL;
    v->len = 0;
    v->cap = 0;
}

static void ast_string_vec_push(AstStringVec *v, const char *item) {
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 8;
        const char **items = (const char **)realloc(v->items, cap * sizeof(const char *));
        if (!items) {
            fprintf(stderr, "ryn: out of memory\n");
            abort();
        }
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = item;
}

static void ast_string_vec_free(AstStringVec *v) {
    free(v->items);
    ast_string_vec_init(v);
}

/* ------------------------------------------------------------------ */
/* Token helpers                                                      */
/* ------------------------------------------------------------------ */

static Token *peek(Parser *p) {
    return &p->tokens->items[p->index];
}

static bool check(Parser *p, TokenKind kind) {
    return peek(p)->kind == kind;
}

static Token *advance(Parser *p) {
    Token *token = peek(p);
    if (token->kind != TOK_EOF) {
        p->index++;
    }
    return token;
}

static bool match(Parser *p, TokenKind kind) {
    if (check(p, kind)) {
        advance(p);
        return true;
    }
    return false;
}

static Token *previous(Parser *p) {
    return p->index > 0 ? &p->tokens->items[p->index - 1] : &p->tokens->items[0];
}

static void error_at(Parser *p, Token *token, DiagCode code, const char *fmt, ...) {
    if (p->fatal) {
        return;
    }
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    int span = token->length > 0 ? token->length : 1;
    diag_error_span(p->diag, token->pos, span, code, "%s", message);
    p->panic_mode = true;
    if (diag_too_many_errors(p->diag)) {
        p->fatal = true;
    }
}

static void error_at_pos(Parser *p, SourcePos pos, DiagCode code, const char *fmt, ...) {
    if (p->fatal) {
        return;
    }
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    diag_error(p->diag, pos, code, "%s", message);
    p->panic_mode = true;
    if (diag_too_many_errors(p->diag)) {
        p->fatal = true;
    }
}

static Token *expect(Parser *p, TokenKind kind, const char *what) {
    if (check(p, kind)) {
        return advance(p);
    }
    char found[128];
    token_describe(peek(p), found, sizeof(found));
    error_at(p, peek(p), E_SYNTAX, "expected %s, found %s", what, found);
    return NULL;
}

static void synchronize(Parser *p) {
    p->panic_mode = false;
    while (!check(p, TOK_EOF)) {
        if (check(p, TOK_NEWLINE)) {
            advance(p);
            return;
        }
        if (check(p, TOK_DEDENT)) {
            return;
        }
        advance(p);
    }
}

static void skip_newlines(Parser *p) {
    while (match(p, TOK_NEWLINE)) {
    }
}

static const char *identifier_text(Parser *p, Token *token) {
    return arena_strndup(p->arena, token->lexeme, (size_t)token->length);
}

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */

static AstType *parse_type(Parser *p);
static AstBlock *parse_block(Parser *p);
static AstStmt *parse_statement(Parser *p);
static AstExpr *parse_expression(Parser *p);
static AstDecl *parse_declaration(Parser *p);

/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */

/* Skips a `<...>` parameter list after a declaration name, reporting one
 * clear diagnostic. Generics are on the roadmap but not part of Ryn 0.1. */
static void skip_generic_parameters(Parser *p) {
    error_at(p, peek(p), E_UNSUPPORTED,
             "generic parameters are not implemented in Ryn 0.1; use concrete types");
    int depth = 0;
    while (!check(p, TOK_EOF)) {
        if (check(p, TOK_LT)) {
            depth++;
        } else if (check(p, TOK_GT)) {
            depth--;
            advance(p);
            if (depth <= 0) {
                break;
            }
            continue;
        }
        advance(p);
    }
}

static AstType *parse_type(Parser *p) {
    Token *token = peek(p);
    SourcePos pos = token->pos;
    if (match(p, TOK_KW_PTR)) {
        if (!expect(p, TOK_LT, "'<' after 'ptr'")) {
            return ast_type_name(p->arena, pos, "<error>");
        }
        AstType *elem = parse_type(p);
        expect(p, TOK_GT, "'>' to close 'ptr<'");
        return ast_type_ptr(p->arena, pos, elem);
    }
    if (match(p, TOK_LBRACKET)) {
        if (check(p, TOK_RBRACKET)) {
            error_at(p, peek(p), E_UNSUPPORTED,
                     "slice types are not implemented in Ryn 0.1; use '[T; N]' or 'ptr<T>'");
            advance(p);
            (void)parse_type(p);
            return ast_type_name(p->arena, pos, "<error>");
        }
        AstType *elem = parse_type(p);
        if (!expect(p, TOK_SEMICOLON, "';' after the array element type")) {
            return ast_type_name(p->arena, pos, "<error>");
        }
        Token *length_token = expect(p, TOK_INT, "an array length");
        int64_t length = 1;
        if (length_token) {
            if (length_token->uval == 0 || length_token->uval > (uint64_t)INT32_MAX) {
                error_at(p, length_token, E_LITERAL_RANGE,
                         "array length must be between 1 and %d", INT32_MAX);
            } else {
                length = (int64_t)length_token->uval;
            }
        }
        expect(p, TOK_RBRACKET, "']' to close the array type");
        return ast_type_array(p->arena, pos, elem, length);
    }
    if (match(p, TOK_KW_FN)) {
        expect(p, TOK_LPAREN, "'(' after 'fn'");
        AstType **params = NULL;
        size_t nparams = 0;
        size_t cap = 0;
        if (!check(p, TOK_RPAREN)) {
            for (;;) {
                AstType *param = parse_type(p);
                if (nparams == cap) {
                    cap = cap ? cap * 2 : 4;
                    AstType **grown = ARENA_ARRAY(p->arena, AstType *, cap);
                    if (params) {
                        memcpy(grown, params, nparams * sizeof(AstType *));
                    }
                    params = grown;
                }
                params[nparams++] = param;
                if (!match(p, TOK_COMMA)) {
                    break;
                }
                if (check(p, TOK_RPAREN)) {
                    break;
                }
            }
        }
        expect(p, TOK_RPAREN, "')' to close the function type");
        AstType *ret = NULL;
        if (match(p, TOK_ARROW)) {
            ret = parse_type(p);
        }
        return ast_type_fn(p->arena, pos, params, nparams, ret);
    }
    if (token_is_type_keyword(token->kind)) {
        advance(p);
        const char *name = NULL;
        switch (token->kind) {
        case TOK_KW_I8: name = "i8"; break;
        case TOK_KW_I16: name = "i16"; break;
        case TOK_KW_I32: name = "i32"; break;
        case TOK_KW_I64: name = "i64"; break;
        case TOK_KW_U8: name = "u8"; break;
        case TOK_KW_U16: name = "u16"; break;
        case TOK_KW_U32: name = "u32"; break;
        case TOK_KW_U64: name = "u64"; break;
        case TOK_KW_F32: name = "f32"; break;
        case TOK_KW_F64: name = "f64"; break;
        case TOK_KW_BOOL: name = "bool"; break;
        case TOK_KW_CHAR: name = "char"; break;
        case TOK_KW_STR: name = "str"; break;
        case TOK_KW_VOID: name = "void"; break;
        default: name = "<error>"; break;
        }
        return ast_type_name(p->arena, pos, name);
    }
    if (match(p, TOK_IDENT)) {
        return ast_type_name(p->arena, pos, identifier_text(p, previous(p)));
    }
    char found[128];
    token_describe(token, found, sizeof(found));
    error_at(p, token, E_SYNTAX, "expected a type, found %s", found);
    if (!check(p, TOK_NEWLINE) && !check(p, TOK_DEDENT) && !check(p, TOK_EOF)) {
        advance(p);
    }
    return ast_type_name(p->arena, pos, "<error>");
}

/* ------------------------------------------------------------------ */
/* Expressions                                                        */
/* ------------------------------------------------------------------ */

static AstExpr *parse_primary(Parser *p) {
    Token *token = peek(p);
    SourcePos pos = token->pos;
    switch (token->kind) {
    case TOK_INT:
        advance(p);
        return ast_int_literal(p->arena, pos, token->ival, token->uval);
    case TOK_FLOAT:
        advance(p);
        return ast_float_literal(p->arena, pos, token->fval);
    case TOK_STRING:
        advance(p);
        return ast_string_literal(p->arena, pos, token->str_value, token->str_length);
    case TOK_CHAR:
        advance(p);
        return ast_char_literal(p->arena, pos, token->ival);
    case TOK_KW_TRUE:
        advance(p);
        return ast_bool_literal(p->arena, pos, true);
    case TOK_KW_FALSE:
        advance(p);
        return ast_bool_literal(p->arena, pos, false);
    case TOK_KW_NULL:
        advance(p);
        return ast_null_literal(p->arena, pos);
    case TOK_LPAREN: {
        advance(p);
        AstExpr *expr = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        return expr;
    }
    case TOK_LBRACKET: {
        advance(p);
        if (match(p, TOK_RBRACKET)) {
            return ast_array_literal(p->arena, pos, NULL, 0, false, 0);
        }
        AstExpr *first = parse_expression(p);
        if (match(p, TOK_SEMICOLON)) {
            Token *count_token = expect(p, TOK_INT, "the number of repetitions");
            int64_t count = 0;
            if (count_token) {
                if (count_token->uval == 0 || count_token->uval > (uint64_t)INT32_MAX) {
                    error_at(p, count_token, E_LITERAL_RANGE,
                             "array repetition count must be between 1 and %d", INT32_MAX);
                    count = 1;
                } else {
                    count = (int64_t)count_token->uval;
                }
            }
            expect(p, TOK_RBRACKET, "']' to close the array literal");
            AstExpr **elements = ARENA_ARRAY(p->arena, AstExpr *, 1);
            elements[0] = first;
            return ast_array_literal(p->arena, pos, elements, 1, true, count);
        }
        AstExprPtrVec elements;
        AstExprPtrVec_init(&elements);
        AstExprPtrVec_push(&elements, first);
        while (match(p, TOK_COMMA)) {
            if (check(p, TOK_RBRACKET)) {
                break;
            }
            AstExprPtrVec_push(&elements, parse_expression(p));
        }
        expect(p, TOK_RBRACKET, "']' to close the array literal");
        AstExpr **element_array = ARENA_ARRAY(p->arena, AstExpr *, elements.len);
        for (size_t i = 0; i < elements.len; i++) {
            element_array[i] = elements.items[i];
        }
        AstExpr *literal =
            ast_array_literal(p->arena, pos, element_array, elements.len, false, 0);
        AstExprPtrVec_free(&elements);
        return literal;
    }
    case TOK_IDENT: {
        advance(p);
        const char *name = identifier_text(p, token);
        if (check(p, TOK_LBRACE)) {
            /* Struct literal: Name { field: value, ... } */
            advance(p);
            AstStringVec names;
            ast_string_vec_init(&names);
            AstExprPtrVec values;
            AstExprPtrVec_init(&values);
            if (!check(p, TOK_RBRACE)) {
                for (;;) {
                    Token *field = expect(p, TOK_IDENT, "a field name");
                    if (!field) {
                        break;
                    }
                    if (!expect(p, TOK_COLON, "':' after the field name")) {
                        break;
                    }
                    AstExpr *value = parse_expression(p);
                    ast_string_vec_push(&names, identifier_text(p, field));
                    AstExprPtrVec_push(&values, value);
                    if (!match(p, TOK_COMMA)) {
                        break;
                    }
                    if (check(p, TOK_RBRACE)) {
                        break;
                    }
                }
            }
            expect(p, TOK_RBRACE, "'}' to close the struct literal");
            const char **name_array = ARENA_ARRAY(p->arena, const char *, names.len);
            AstExpr **value_array = ARENA_ARRAY(p->arena, AstExpr *, values.len);
            for (size_t i = 0; i < names.len; i++) {
                name_array[i] = names.items[i];
                value_array[i] = values.items[i];
            }
            AstExpr *expr = ast_struct_literal(p->arena, pos, ast_type_name(p->arena, pos, name),
                                               name_array, value_array, names.len);
            ast_string_vec_free(&names);
            AstExprPtrVec_free(&values);
            return expr;
        }
        return ast_ident(p->arena, pos, name);
    }
    default:
        break;
    }
    {
        char found[128];
        token_describe(token, found, sizeof(found));
        error_at(p, token, E_SYNTAX, "expected an expression, found %s", found);
        if (!check(p, TOK_EOF) && !check(p, TOK_NEWLINE) && !check(p, TOK_DEDENT)) {
            advance(p);
        }
        return ast_error_expr(p->arena, pos);
    }
}

static AstExpr *parse_postfix(Parser *p) {
    AstExpr *expr = parse_primary(p);
    for (;;) {
        if (check(p, TOK_LPAREN)) {
            Token *open = advance(p);
            AstExprPtrVec args;
            AstExprPtrVec_init(&args);
            if (!check(p, TOK_RPAREN)) {
                for (;;) {
                    AstExprPtrVec_push(&args, parse_expression(p));
                    if (!match(p, TOK_COMMA)) {
                        break;
                    }
                    if (check(p, TOK_RPAREN)) {
                        break;
                    }
                }
            }
            expect(p, TOK_RPAREN, "')' to close the call");
            AstExpr **arg_array = ARENA_ARRAY(p->arena, AstExpr *, args.len);
            for (size_t i = 0; i < args.len; i++) {
                arg_array[i] = args.items[i];
            }
            expr = ast_call(p->arena, open->pos, expr, arg_array, args.len);
            AstExprPtrVec_free(&args);
            continue;
        }
        if (match(p, TOK_DOT)) {
            Token *name = expect(p, TOK_IDENT, "a field or module member name");
            if (!name) {
                return expr;
            }
            expr = ast_field(p->arena, name->pos, expr, identifier_text(p, name));
            continue;
        }
        if (match(p, TOK_LBRACKET)) {
            AstExpr *index = parse_expression(p);
            expect(p, TOK_RBRACKET, "']'");
            expr = ast_index(p->arena, expr->pos, expr, index);
            continue;
        }
        if (match(p, TOK_KW_AS)) {
            AstType *type = parse_type(p);
            expr = ast_cast(p->arena, expr->pos, expr, type);
            continue;
        }
        break;
    }
    return expr;
}

static AstExpr *parse_unary(Parser *p) {
    Token *token = peek(p);
    SourcePos pos = token->pos;
    UnOp op;
    switch (token->kind) {
    case TOK_MINUS:
        op = UN_NEG;
        break;
    case TOK_BANG:
        op = UN_NOT;
        break;
    case TOK_TILDE:
        op = UN_BNOT;
        break;
    case TOK_AMP:
        op = UN_ADDR;
        break;
    case TOK_STAR:
        op = UN_DEREF;
        break;
    case TOK_PLUS:
        advance(p);
        return parse_unary(p);
    default:
        return parse_postfix(p);
    }
    advance(p);
    AstExpr *operand = parse_unary(p);
    return ast_unary(p->arena, pos, op, operand);
}

static AstExpr *make_binary(Parser *p, AstExpr *lhs, BinOp op, AstExpr *rhs) {
    return ast_binary(p->arena, lhs->pos, op, lhs, rhs);
}

static AstExpr *parse_mul(Parser *p) {
    AstExpr *lhs = parse_unary(p);
    for (;;) {
        BinOp op;
        if (match(p, TOK_STAR)) {
            op = BIN_MUL;
        } else if (match(p, TOK_SLASH)) {
            op = BIN_DIV;
        } else if (match(p, TOK_PERCENT)) {
            op = BIN_MOD;
        } else {
            break;
        }
        lhs = make_binary(p, lhs, op, parse_unary(p));
    }
    return lhs;
}

static AstExpr *parse_add(Parser *p) {
    AstExpr *lhs = parse_mul(p);
    for (;;) {
        BinOp op;
        if (match(p, TOK_PLUS)) {
            op = BIN_ADD;
        } else if (match(p, TOK_MINUS)) {
            op = BIN_SUB;
        } else {
            break;
        }
        lhs = make_binary(p, lhs, op, parse_mul(p));
    }
    return lhs;
}

static AstExpr *parse_shift(Parser *p) {
    AstExpr *lhs = parse_add(p);
    for (;;) {
        BinOp op;
        if (match(p, TOK_SHL)) {
            op = BIN_SHL;
        } else if (match(p, TOK_SHR)) {
            op = BIN_SHR;
        } else {
            break;
        }
        lhs = make_binary(p, lhs, op, parse_add(p));
    }
    return lhs;
}

static AstExpr *parse_bitand(Parser *p) {
    AstExpr *lhs = parse_shift(p);
    while (match(p, TOK_AMP)) {
        lhs = make_binary(p, lhs, BIN_AND, parse_shift(p));
    }
    return lhs;
}

static AstExpr *parse_bitxor(Parser *p) {
    AstExpr *lhs = parse_bitand(p);
    while (match(p, TOK_CARET)) {
        lhs = make_binary(p, lhs, BIN_XOR, parse_bitand(p));
    }
    return lhs;
}

static AstExpr *parse_bitor(Parser *p) {
    AstExpr *lhs = parse_bitxor(p);
    while (match(p, TOK_PIPE)) {
        lhs = make_binary(p, lhs, BIN_OR, parse_bitxor(p));
    }
    return lhs;
}

static AstExpr *parse_comparison(Parser *p) {
    AstExpr *lhs = parse_bitor(p);
    for (;;) {
        BinOp op;
        if (match(p, TOK_EQ)) {
            op = BIN_EQ;
        } else if (match(p, TOK_NE)) {
            op = BIN_NE;
        } else if (match(p, TOK_LT)) {
            op = BIN_LT;
        } else if (match(p, TOK_LE)) {
            op = BIN_LE;
        } else if (match(p, TOK_GT)) {
            op = BIN_GT;
        } else if (match(p, TOK_GE)) {
            op = BIN_GE;
        } else {
            break;
        }
        lhs = make_binary(p, lhs, op, parse_bitor(p));
    }
    return lhs;
}

static AstExpr *parse_not(Parser *p) {
    if (check(p, TOK_KW_NOT)) {
        Token *token = advance(p);
        return ast_unary(p->arena, token->pos, UN_NOT, parse_not(p));
    }
    return parse_comparison(p);
}

static AstExpr *parse_and(Parser *p) {
    AstExpr *lhs = parse_not(p);
    while (check(p, TOK_KW_AND) || check(p, TOK_AMPAMP)) {
        advance(p);
        lhs = make_binary(p, lhs, BIN_LAND, parse_not(p));
    }
    return lhs;
}

static AstExpr *parse_or(Parser *p) {
    AstExpr *lhs = parse_and(p);
    while (check(p, TOK_KW_OR) || check(p, TOK_PIPEPIPE)) {
        advance(p);
        lhs = make_binary(p, lhs, BIN_LOR, parse_and(p));
    }
    return lhs;
}

static AstExpr *parse_expression(Parser *p) {
    return parse_or(p);
}

/* ------------------------------------------------------------------ */
/* Statements                                                         */
/* ------------------------------------------------------------------ */

static void expect_statement_end(Parser *p) {
    if (match(p, TOK_NEWLINE)) {
        return;
    }
    if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) {
        return;
    }
    char found[128];
    token_describe(peek(p), found, sizeof(found));
    error_at(p, peek(p), E_SYNTAX, "expected end of line, found %s", found);
    synchronize(p);
}

static AstStmt *parse_var_decl(Parser *p, bool is_mut) {
    Token *keyword = previous(p);
    Token *name_token = expect(p, TOK_IDENT, "a variable name");
    if (!name_token) {
        synchronize(p);
        return NULL;
    }
    const char *name = identifier_text(p, name_token);
    AstType *type = NULL;
    if (match(p, TOK_COLON)) {
        type = parse_type(p);
    }
    AstExpr *init = NULL;
    if (match(p, TOK_ASSIGN)) {
        init = parse_expression(p);
    }
    if (!type && !init) {
        error_at_pos(p, keyword->pos, E_SYNTAX,
                     "variable '%s' needs a type annotation or an initializer", name);
    }
    expect_statement_end(p);
    return ast_var_decl(p->arena, keyword->pos, is_mut, name, type, init);
}

static AstStmt *parse_if(Parser *p) {
    Token *keyword = advance(p);
    AstExpr *cond = parse_expression(p);
    expect(p, TOK_COLON, "':' after the condition");
    AstBlock *then_block = parse_block(p);
    AstStmt *else_branch = NULL;
    if (check(p, TOK_KW_ELSE)) {
        advance(p);
        if (check(p, TOK_KW_IF)) {
            else_branch = parse_if(p);
        } else {
            SourcePos pos = peek(p)->pos;
            expect(p, TOK_COLON, "':' after 'else'");
            AstBlock *else_block = parse_block(p);
            else_branch = ast_block_stmt(p->arena, pos, else_block);
        }
    }
    return ast_if(p->arena, keyword->pos, cond, then_block, else_branch);
}

static AstStmt *parse_while(Parser *p) {
    Token *keyword = advance(p);
    AstExpr *cond = parse_expression(p);
    expect(p, TOK_COLON, "':' after the condition");
    AstBlock *body = parse_block(p);
    return ast_while(p->arena, keyword->pos, cond, body);
}

static AstStmt *parse_for(Parser *p) {
    Token *keyword = advance(p);
    Token *var = expect(p, TOK_IDENT, "a loop variable name");
    if (!var) {
        synchronize(p);
        return NULL;
    }
    if (!expect(p, TOK_KW_IN, "'in' after the loop variable")) {
        synchronize(p);
        return NULL;
    }
    AstExpr *start = parse_expression(p);
    bool inclusive;
    if (match(p, TOK_DOTDOT)) {
        inclusive = false;
    } else if (match(p, TOK_DOTDOTEQ)) {
        inclusive = true;
    } else {
        error_at(p, peek(p), E_SYNTAX, "expected '..' or '..=' in for loop");
        synchronize(p);
        return NULL;
    }
    AstExpr *end = parse_expression(p);
    expect(p, TOK_COLON, "':' after the range");
    AstBlock *body = parse_block(p);
    return ast_for(p->arena, keyword->pos, identifier_text(p, var), var->pos, start, end, inclusive,
                   body);
}

static AstStmt *parse_statement(Parser *p) {
    Token *token = peek(p);
    switch (token->kind) {
    case TOK_KW_LET:
        advance(p);
        return parse_var_decl(p, false);
    case TOK_KW_VAR:
        advance(p);
        return parse_var_decl(p, true);
    case TOK_KW_IF:
        return parse_if(p);
    case TOK_KW_WHILE:
        return parse_while(p);
    case TOK_KW_FOR:
        return parse_for(p);
    case TOK_KW_RETURN: {
        advance(p);
        AstExpr *value = NULL;
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_DEDENT) && !check(p, TOK_EOF) &&
            !check(p, TOK_KW_ELSE)) {
            value = parse_expression(p);
        }
        expect_statement_end(p);
        return ast_return(p->arena, token->pos, value);
    }
    case TOK_KW_BREAK:
        advance(p);
        expect_statement_end(p);
        return ast_break(p->arena, token->pos);
    case TOK_KW_CONTINUE:
        advance(p);
        expect_statement_end(p);
        return ast_continue(p->arena, token->pos);
    case TOK_KW_STRUCT:
    case TOK_KW_ENUM:
    case TOK_KW_FN:
    case TOK_KW_IMPORT:
        error_at(p, token, E_SYNTAX,
                 "declarations are only allowed at the top level (unindent to end the block)");
        synchronize(p);
        return NULL;
    default:
        break;
    }

    AstExpr *expr = parse_expression(p);
    if (token_is_assign_op(peek(p)->kind)) {
        Token *op_token = advance(p);
        BinOp op = BIN_NONE;
        switch (op_token->kind) {
        case TOK_PLUS_ASSIGN: op = BIN_ADD; break;
        case TOK_MINUS_ASSIGN: op = BIN_SUB; break;
        case TOK_STAR_ASSIGN: op = BIN_MUL; break;
        case TOK_SLASH_ASSIGN: op = BIN_DIV; break;
        case TOK_PERCENT_ASSIGN: op = BIN_MOD; break;
        default: op = BIN_NONE; break;
        }
        AstExpr *value = parse_expression(p);
        expect_statement_end(p);
        return ast_assign(p->arena, token->pos, expr, value, op);
    }
    expect_statement_end(p);
    return ast_expr_stmt(p->arena, token->pos, expr);
}

static AstBlock *parse_block(Parser *p) {
    SourcePos pos = peek(p)->pos;
    AstBlock *block = ast_block_new(p->arena, pos);
    if (!expect(p, TOK_NEWLINE, "a newline after ':'")) {
        return block;
    }
    if (!expect(p, TOK_INDENT, "an indented block after ':'")) {
        return block;
    }
    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF) && !p->fatal) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) {
            break;
        }
        size_t before = p->index;
        AstStmt *stmt = parse_statement(p);
        if (stmt) {
            ast_block_add(block, stmt);
        }
        if (p->index == before) {
            advance(p);
        }
    }
    if (!match(p, TOK_DEDENT) && check(p, TOK_EOF)) {
        error_at(p, peek(p), E_SYNTAX, "unexpected end of file: block is not closed");
    }
    return block;
}

/* ------------------------------------------------------------------ */
/* Declarations                                                       */
/* ------------------------------------------------------------------ */

static AstDecl *parse_fn_decl(Parser *p, bool is_extern, SourcePos keyword_pos) {
    Token *name = expect(p, TOK_IDENT, "a function name");
    if (!name) {
        synchronize(p);
        return NULL;
    }
    if (check(p, TOK_LT)) {
        skip_generic_parameters(p);
    }
    if (!expect(p, TOK_LPAREN, "'(' after the function name")) {
        synchronize(p);
        return NULL;
    }
    AstParamVec params;
    AstParamVec_init(&params);
    if (!check(p, TOK_RPAREN)) {
        for (;;) {
            Token *param_name = expect(p, TOK_IDENT, "a parameter name");
            if (!param_name) {
                break;
            }
            AstParam param;
            memset(&param, 0, sizeof(param));
            param.name = identifier_text(p, param_name);
            param.pos = param_name->pos;
            if (!expect(p, TOK_COLON, "':' after the parameter name")) {
                break;
            }
            param.type_syntax = parse_type(p);
            AstParamVec_push(&params, param);
            if (!match(p, TOK_COMMA)) {
                break;
            }
            if (check(p, TOK_RPAREN)) {
                break;
            }
        }
    }
    if (!expect(p, TOK_RPAREN, "')' after the parameter list")) {
        AstParamVec_free(&params);
        synchronize(p);
        return NULL;
    }
    AstType *return_type = NULL;
    if (match(p, TOK_ARROW)) {
        return_type = parse_type(p);
    }
    AstParam *param_array = ARENA_ARRAY(p->arena, AstParam, params.len);
    for (size_t i = 0; i < params.len; i++) {
        param_array[i] = params.items[i];
    }
    size_t nparams = params.len;
    AstParamVec_free(&params);

    AstBlock *body = NULL;
    if (is_extern) {
        if (check(p, TOK_COLON)) {
            error_at(p, peek(p), E_SYNTAX, "extern functions cannot have a body");
            advance(p);
            parse_block(p);
        } else {
            expect_statement_end(p);
        }
    } else {
        expect(p, TOK_COLON, "':' before the function body");
        body = parse_block(p);
    }
    return ast_fn_decl(p->arena, keyword_pos, identifier_text(p, name), param_array, nparams,
                       return_type, body, is_extern);
}

static AstDecl *parse_struct_decl(Parser *p) {
    Token *keyword = advance(p);
    Token *name = expect(p, TOK_IDENT, "a struct name");
    if (!name) {
        synchronize(p);
        return NULL;
    }
    const char *struct_name = identifier_text(p, name);
    if (check(p, TOK_LT)) {
        skip_generic_parameters(p);
    }
    if (!expect(p, TOK_COLON, "':' after the struct name")) {
        synchronize(p);
        return NULL;
    }
    if (!expect(p, TOK_NEWLINE, "a newline after ':'")) {
        synchronize(p);
        return NULL;
    }
    if (!expect(p, TOK_INDENT, "an indented field list")) {
        synchronize(p);
        return NULL;
    }
    AstFieldDeclVec fields;
    AstFieldDeclVec_init(&fields);
    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF) && !p->fatal) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) {
            break;
        }
        Token *field_name = expect(p, TOK_IDENT, "a field name");
        if (!field_name) {
            synchronize(p);
            continue;
        }
        if (!expect(p, TOK_COLON, "':' after the field name")) {
            synchronize(p);
            continue;
        }
        AstFieldDecl field;
        field.name = identifier_text(p, field_name);
        field.pos = field_name->pos;
        field.type_syntax = parse_type(p);
        AstFieldDeclVec_push(&fields, field);
        if (!match(p, TOK_NEWLINE)) {
            break;
        }
    }
    if (!match(p, TOK_DEDENT)) {
        error_at(p, peek(p), E_SYNTAX, "expected the end of the struct body");
        synchronize(p);
    }
    if (fields.len == 0) {
        error_at_pos(p, keyword->pos, E_SYNTAX, "struct '%s' must have at least one field",
                     struct_name);
    }
    AstFieldDecl *field_array = ARENA_ARRAY(p->arena, AstFieldDecl, fields.len);
    for (size_t i = 0; i < fields.len; i++) {
        field_array[i] = fields.items[i];
    }
    size_t nfields = fields.len;
    AstFieldDeclVec_free(&fields);
    return ast_struct_decl(p->arena, keyword->pos, struct_name, field_array, nfields);
}

static AstDecl *parse_enum_decl(Parser *p) {
    Token *keyword = advance(p);
    Token *name = expect(p, TOK_IDENT, "an enum name");
    if (!name) {
        synchronize(p);
        return NULL;
    }
    const char *enum_name = identifier_text(p, name);
    if (check(p, TOK_LT)) {
        skip_generic_parameters(p);
    }
    if (!expect(p, TOK_COLON, "':' after the enum name")) {
        synchronize(p);
        return NULL;
    }
    if (!expect(p, TOK_NEWLINE, "a newline after ':'")) {
        synchronize(p);
        return NULL;
    }
    if (!expect(p, TOK_INDENT, "an indented variant list")) {
        synchronize(p);
        return NULL;
    }
    AstEnumVariantVec variants;
    AstEnumVariantVec_init(&variants);
    int64_t next_value = 0;
    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF) && !p->fatal) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) {
            break;
        }
        Token *variant_name = expect(p, TOK_IDENT, "a variant name");
        if (!variant_name) {
            synchronize(p);
            continue;
        }
        AstEnumVariant variant;
        memset(&variant, 0, sizeof(variant));
        variant.name = identifier_text(p, variant_name);
        variant.pos = variant_name->pos;
        variant.value = next_value;
        if (match(p, TOK_ASSIGN)) {
            bool negative = match(p, TOK_MINUS);
            Token *value_token = expect(p, TOK_INT, "an integer variant value");
            if (value_token) {
                variant.value = negative ? -value_token->ival : value_token->ival;
                variant.has_value = true;
            }
        }
        next_value = variant.value + 1;
        AstEnumVariantVec_push(&variants, variant);
        if (!match(p, TOK_NEWLINE)) {
            break;
        }
    }
    if (!match(p, TOK_DEDENT)) {
        error_at(p, peek(p), E_SYNTAX, "expected the end of the enum body");
        synchronize(p);
    }
    if (variants.len == 0) {
        error_at_pos(p, keyword->pos, E_SYNTAX, "enum '%s' must have at least one variant",
                     enum_name);
    }
    AstEnumVariant *variant_array = ARENA_ARRAY(p->arena, AstEnumVariant, variants.len);
    for (size_t i = 0; i < variants.len; i++) {
        variant_array[i] = variants.items[i];
    }
    size_t nvariants = variants.len;
    AstEnumVariantVec_free(&variants);
    return ast_enum_decl(p->arena, keyword->pos, enum_name, variant_array, nvariants);
}

static AstDecl *parse_import_decl(Parser *p) {
    Token *keyword = advance(p);
    Token *name = expect(p, TOK_IDENT, "a module name");
    if (!name) {
        synchronize(p);
        return NULL;
    }
    if (check(p, TOK_DOT)) {
        error_at(p, peek(p), E_SYNTAX,
                 "nested module paths are not supported; import a top level module");
        while (match(p, TOK_DOT)) {
            expect(p, TOK_IDENT, "a module name");
        }
    }
    expect_statement_end(p);
    return ast_import_decl(p->arena, keyword->pos, identifier_text(p, name));
}

static AstDecl *parse_declaration(Parser *p) {
    Token *token = peek(p);
    switch (token->kind) {
    case TOK_KW_FN: {
        advance(p);
        return parse_fn_decl(p, false, token->pos);
    }
    case TOK_KW_EXTERN: {
        advance(p);
        if (!expect(p, TOK_KW_FN, "'fn' after 'extern'")) {
            synchronize(p);
            return NULL;
        }
        return parse_fn_decl(p, true, token->pos);
    }
    case TOK_KW_STRUCT:
        return parse_struct_decl(p);
    case TOK_KW_ENUM:
        return parse_enum_decl(p);
    case TOK_KW_IMPORT:
        return parse_import_decl(p);
    default: {
        char found[128];
        token_describe(token, found, sizeof(found));
        error_at(p, token, E_SYNTAX,
                 "expected a declaration ('fn', 'struct', 'enum' or 'import'), found %s", found);
        synchronize(p);
        return NULL;
    }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

void parser_init(Parser *parser, Arena *arena, Diagnostics *diag, SourceManager *sm,
                 TokenVec *tokens) {
    memset(parser, 0, sizeof(*parser));
    parser->arena = arena;
    parser->diag = diag;
    parser->sm = sm;
    parser->tokens = tokens;
    parser->errors_at_start = diag->error_count;
}

AstProgram *parser_parse_program(Parser *parser, SourceFile *file) {
    RYN_UNUSED(file);
    AstProgram *program = ast_program_new(parser->arena);
    bool seen_non_import = false;
    while (!check(parser, TOK_EOF) && !parser->fatal) {
        skip_newlines(parser);
        if (check(parser, TOK_EOF) || parser->fatal) {
            break;
        }
        size_t before = parser->index;
        AstDecl *decl = parse_declaration(parser);
        if (decl) {
            if (decl->kind == AST_IMPORT_DECL) {
                if (seen_non_import) {
                    error_at_pos(parser, decl->pos, E_IMPORT_POSITION,
                                 "imports must appear before any other declaration");
                }
            } else {
                seen_non_import = true;
            }
            ast_program_add(program, decl);
        }
        if (parser->index == before) {
            advance(parser);
        }
    }
    return program;
}
