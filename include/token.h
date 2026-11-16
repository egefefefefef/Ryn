/*
 * Ryn programming language — tokens.
 *
 * The lexer produces a flat token stream. Layout is encoded with explicit
 * TOK_NEWLINE / TOK_INDENT / TOK_DEDENT tokens so the parser does not need to
 * deal with raw whitespace.
 */
#ifndef RYN_TOKEN_H
#define RYN_TOKEN_H

#include "common.h"
#include "source.h"

typedef enum TokenKind {
    TOK_EOF = 0,
    TOK_NEWLINE,
    TOK_INDENT,
    TOK_DEDENT,

    TOK_IDENT,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_CHAR,

    /* Keywords. */
    TOK_KW_LET,
    TOK_KW_VAR,
    TOK_KW_FN,
    TOK_KW_RETURN,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_FOR,
    TOK_KW_IN,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_STRUCT,
    TOK_KW_ENUM,
    TOK_KW_IMPORT,
    TOK_KW_AS,
    TOK_KW_AND,
    TOK_KW_OR,
    TOK_KW_NOT,
    TOK_KW_TRUE,
    TOK_KW_FALSE,
    TOK_KW_NULL,
    TOK_KW_EXTERN,

    /* Built-in type keywords. */
    TOK_KW_I8,
    TOK_KW_I16,
    TOK_KW_I32,
    TOK_KW_I64,
    TOK_KW_U8,
    TOK_KW_U16,
    TOK_KW_U32,
    TOK_KW_U64,
    TOK_KW_F32,
    TOK_KW_F64,
    TOK_KW_BOOL,
    TOK_KW_CHAR,
    TOK_KW_STR,
    TOK_KW_VOID,
    TOK_KW_PTR,

    /* Punctuation and operators. */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_COMMA,
    TOK_DOT,
    TOK_DOTDOT,     /* ..  exclusive range */
    TOK_DOTDOTEQ,   /* ..= inclusive range */
    TOK_ARROW,      /* -> */

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_ASSIGN,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_AMP,
    TOK_AMPAMP,
    TOK_PIPE,
    TOK_PIPEPIPE,
    TOK_CARET,
    TOK_TILDE,
    TOK_BANG,
    TOK_SHL,
    TOK_SHR,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN,
    TOK_SLASH_ASSIGN,
    TOK_PERCENT_ASSIGN,

    TOK_KIND_COUNT
} TokenKind;

typedef struct Token {
    TokenKind kind;
    SourcePos pos;
    const char *lexeme; /* points into the source buffer (not NUL terminated) */
    int length;
    int64_t ival;       /* integer literal value; for chars, the byte value */
    uint64_t uval;      /* same bits as ival, convenient for unsigned checks */
    double fval;        /* float literal value */
    const char *str_value; /* decoded string literal (arena owned) */
    size_t str_length;
} Token;

RYN_DEFINE_VEC(TokenVec, Token)

const char *token_kind_name(TokenKind kind);
bool token_is_keyword(TokenKind kind);
bool token_is_type_keyword(TokenKind kind);
bool token_is_literal(TokenKind kind);
bool token_is_assign_op(TokenKind kind);

/* Human readable spelling of a token, quoted for names and literals. */
void token_describe(const Token *token, char *buf, size_t buflen);

#endif /* RYN_TOKEN_H */
