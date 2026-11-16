#include "lexer.h"

#include <ctype.h>
#include <errno.h>

typedef struct KeywordEntry {
    const char *text;
    TokenKind kind;
} KeywordEntry;

static const KeywordEntry KEYWORDS[] = {
    {"let", TOK_KW_LET},
    {"var", TOK_KW_VAR},
    {"fn", TOK_KW_FN},
    {"return", TOK_KW_RETURN},
    {"if", TOK_KW_IF},
    {"else", TOK_KW_ELSE},
    {"while", TOK_KW_WHILE},
    {"for", TOK_KW_FOR},
    {"in", TOK_KW_IN},
    {"break", TOK_KW_BREAK},
    {"continue", TOK_KW_CONTINUE},
    {"struct", TOK_KW_STRUCT},
    {"enum", TOK_KW_ENUM},
    {"import", TOK_KW_IMPORT},
    {"as", TOK_KW_AS},
    {"and", TOK_KW_AND},
    {"or", TOK_KW_OR},
    {"not", TOK_KW_NOT},
    {"true", TOK_KW_TRUE},
    {"false", TOK_KW_FALSE},
    {"null", TOK_KW_NULL},
    {"extern", TOK_KW_EXTERN},
    {"i8", TOK_KW_I8},
    {"i16", TOK_KW_I16},
    {"i32", TOK_KW_I32},
    {"i64", TOK_KW_I64},
    {"u8", TOK_KW_U8},
    {"u16", TOK_KW_U16},
    {"u32", TOK_KW_U32},
    {"u64", TOK_KW_U64},
    {"f32", TOK_KW_F32},
    {"f64", TOK_KW_F64},
    {"bool", TOK_KW_BOOL},
    {"char", TOK_KW_CHAR},
    {"str", TOK_KW_STR},
    {"void", TOK_KW_VOID},
    {"ptr", TOK_KW_PTR},
};

static const char *const KIND_NAMES[TOK_KIND_COUNT] = {
    "end of file",   "newline",       "indent",        "dedent",
    "identifier",    "integer",       "float",         "string",
    "char",          "'let'",         "'var'",         "'fn'",
    "'return'",      "'if'",          "'else'",        "'while'",
    "'for'",         "'in'",          "'break'",       "'continue'",
    "'struct'",      "'enum'",        "'import'",      "'as'",
    "'and'",         "'or'",          "'not'",         "'true'",
    "'false'",       "'null'",        "'extern'",      "'i8'",
    "'i16'",         "'i32'",         "'i64'",         "'u8'",
    "'u16'",         "'u32'",         "'u64'",         "'f32'",
    "'f64'",         "'bool'",        "'char'",        "'str'",
    "'void'",        "'ptr'",         "'('",           "')'",
    "'['",           "']'",           "'{'",           "'}'",
    "':'",           "';'",           "','",           "'.'",           "'..'",
    "'..='",         "'->'",          "'+'",           "'-'",
    "'*'",           "'/'",           "'%'",           "'='",
    "'=='",          "'!='",          "'<'",           "'<='",
    "'>'",           "'>='",          "'&'",           "'&&'",
    "'|'",           "'||'",          "'^'",           "'~'",
    "'!'",           "'<<'",          "'>>'",          "'+='",
    "'-='",          "'*='",          "'/='",          "'%='",
};

const char *token_kind_name(TokenKind kind) {
    if ((int)kind < 0 || kind >= TOK_KIND_COUNT) {
        return "unknown token";
    }
    return KIND_NAMES[kind];
}

bool token_is_keyword(TokenKind kind) {
    return kind >= TOK_KW_LET && kind <= TOK_KW_EXTERN;
}

bool token_is_type_keyword(TokenKind kind) {
    return kind >= TOK_KW_I8 && kind <= TOK_KW_PTR;
}

bool token_is_literal(TokenKind kind) {
    switch (kind) {
    case TOK_INT:
    case TOK_FLOAT:
    case TOK_STRING:
    case TOK_CHAR:
    case TOK_KW_TRUE:
    case TOK_KW_FALSE:
    case TOK_KW_NULL:
        return true;
    default:
        return false;
    }
}

bool token_is_assign_op(TokenKind kind) {
    switch (kind) {
    case TOK_ASSIGN:
    case TOK_PLUS_ASSIGN:
    case TOK_MINUS_ASSIGN:
    case TOK_STAR_ASSIGN:
    case TOK_SLASH_ASSIGN:
    case TOK_PERCENT_ASSIGN:
        return true;
    default:
        return false;
    }
}

void token_describe(const Token *token, char *buf, size_t buflen) {
    if (!token) {
        snprintf(buf, buflen, "unknown token");
        return;
    }
    switch (token->kind) {
    case TOK_IDENT:
        snprintf(buf, buflen, "'%.*s'", token->length, token->lexeme);
        break;
    case TOK_INT:
        snprintf(buf, buflen, "integer literal '%.*s'", token->length, token->lexeme);
        break;
    case TOK_FLOAT:
        snprintf(buf, buflen, "float literal '%.*s'", token->length, token->lexeme);
        break;
    case TOK_STRING:
        snprintf(buf, buflen, "string literal");
        break;
    case TOK_CHAR:
        snprintf(buf, buflen, "char literal");
        break;
    case TOK_NEWLINE:
        snprintf(buf, buflen, "end of line");
        break;
    case TOK_INDENT:
        snprintf(buf, buflen, "indentation");
        break;
    case TOK_DEDENT:
        snprintf(buf, buflen, "end of block");
        break;
    case TOK_EOF:
        snprintf(buf, buflen, "end of file");
        break;
    default:
        snprintf(buf, buflen, "%s", token_kind_name(token->kind));
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Lexer implementation                                               */
/* ------------------------------------------------------------------ */

typedef struct Bracket {
    char open;
    SourcePos pos;
} Bracket;

#define MAX_INDENT_LEVELS 256
#define MAX_BRACKETS 256

static char peek_at(Lexer *l, size_t offset) {
    size_t at = l->pos + offset;
    return at < l->len ? l->src[at] : '\0';
}

static char peek(Lexer *l) {
    return peek_at(l, 0);
}

static SourcePos current_pos(Lexer *l) {
    SourcePos pos;
    pos.file = l->file->id;
    pos.line = l->line;
    pos.column = l->column;
    pos.offset = l->pos;
    return pos;
}

static void advance(Lexer *l) {
    if (l->pos >= l->len) {
        return;
    }
    char c = l->src[l->pos++];
    if (c == '\n') {
        l->line++;
        l->column = 1;
    } else {
        l->column++;
    }
}

static Token *push_token(Lexer *l, TokenKind kind, SourcePos pos, size_t start, size_t length) {
    Token token;
    memset(&token, 0, sizeof(token));
    token.kind = kind;
    token.pos = pos;
    token.lexeme = start <= l->len ? l->src + start : NULL;
    token.length = (int)length;
    TokenVec_push(&l->tokens, token);
    return &l->tokens.items[l->tokens.len - 1];
}

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_continue(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static void skip_line_comment(Lexer *l) {
    while (l->pos < l->len && peek(l) != '\n') {
        advance(l);
    }
}

static void skip_block_comment(Lexer *l, SourcePos start) {
    /* current position is at '#[' */
    advance(l);
    advance(l);
    int depth = 1;
    while (l->pos < l->len) {
        if (peek(l) == '#' && peek_at(l, 1) == '[') {
            depth++;
            advance(l);
            advance(l);
        } else if (peek(l) == ']' && peek_at(l, 1) == '#') {
            depth--;
            advance(l);
            advance(l);
            if (depth == 0) {
                return;
            }
        } else {
            advance(l);
        }
    }
    diag_error(l->diag, start, E_SYNTAX, "unterminated block comment");
}

/* Skips a comment; returns true when the comment spanned a newline. */
static bool skip_comment(Lexer *l) {
    int line_before = l->line;
    SourcePos start = current_pos(l);
    if (peek_at(l, 1) == '[') {
        skip_block_comment(l, start);
    } else {
        skip_line_comment(l);
    }
    return l->line != line_before;
}

static bool append_utf8(StrBuf *sb, uint32_t cp) {
    if (cp <= 0x7F) {
        sb_append_char(sb, (char)cp);
    } else if (cp <= 0x7FF) {
        sb_append_char(sb, (char)(0xC0 | (cp >> 6)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        sb_append_char(sb, (char)(0xE0 | (cp >> 12)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        sb_append_char(sb, (char)(0xF0 | (cp >> 18)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        return false;
    }
    return true;
}

/* Decodes an escape sequence. The leading backslash has already been
 * consumed. Returns false when the escape is invalid (already diagnosed). */
static bool decode_escape(Lexer *l, StrBuf *sb, bool for_char) {
    SourcePos pos = current_pos(l);
    if (l->pos >= l->len) {
        diag_error(l->diag, pos, E_SYNTAX, "unterminated escape sequence");
        return false;
    }
    char c = peek(l);
    switch (c) {
    case 'n':
        sb_append_char(sb, '\n');
        advance(l);
        return true;
    case 't':
        sb_append_char(sb, '\t');
        advance(l);
        return true;
    case 'r':
        sb_append_char(sb, '\r');
        advance(l);
        return true;
    case '0':
        sb_append_char(sb, '\0');
        advance(l);
        return true;
    case 'a':
        sb_append_char(sb, '\a');
        advance(l);
        return true;
    case 'b':
        sb_append_char(sb, '\b');
        advance(l);
        return true;
    case 'f':
        sb_append_char(sb, '\f');
        advance(l);
        return true;
    case 'v':
        sb_append_char(sb, '\v');
        advance(l);
        return true;
    case '\\':
        sb_append_char(sb, '\\');
        advance(l);
        return true;
    case '\'':
        sb_append_char(sb, '\'');
        advance(l);
        return true;
    case '"':
        sb_append_char(sb, '"');
        advance(l);
        return true;
    case 'x': {
        advance(l);
        int value = 0;
        int digits = 0;
        while (digits < 2 && isxdigit((unsigned char)peek(l))) {
            char d = peek(l);
            value = value * 16 + (isdigit((unsigned char)d) ? d - '0' : (tolower((unsigned char)d) - 'a' + 10));
            digits++;
            advance(l);
        }
        if (digits != 2) {
            diag_error(l->diag, pos, E_SYNTAX, "\\x escape needs exactly two hexadecimal digits");
            return false;
        }
        sb_append_char(sb, (char)(unsigned char)value);
        return true;
    }
    case 'u': {
        advance(l);
        if (peek(l) != '{') {
            diag_error(l->diag, pos, E_SYNTAX, "\\u escape must be written as \\u{XXXX}");
            return false;
        }
        advance(l);
        uint32_t value = 0;
        int digits = 0;
        while (digits < 8 && isxdigit((unsigned char)peek(l))) {
            char d = peek(l);
            value = value * 16 + (uint32_t)(isdigit((unsigned char)d) ? d - '0'
                                                                    : (tolower((unsigned char)d) - 'a' + 10));
            digits++;
            advance(l);
        }
        if (digits == 0 || peek(l) != '}') {
            diag_error(l->diag, pos, E_SYNTAX, "invalid \\u{...} escape");
            return false;
        }
        advance(l);
        if (for_char && value > 0xFF) {
            diag_error(l->diag, pos, E_LITERAL_RANGE,
                       "char is a single byte; value \\u{%X} does not fit (use a str)", value);
            return false;
        }
        if (!append_utf8(sb, value)) {
            diag_error(l->diag, pos, E_LITERAL_RANGE, "\\u{%X} is not a valid Unicode scalar value",
                       value);
            return false;
        }
        return true;
    }
    default:
        diag_error(l->diag, pos, E_SYNTAX, "unknown escape sequence '\\%c'", c);
        advance(l);
        return false;
    }
}

static void scan_string(Lexer *l, SourcePos start) {
    size_t content_start = l->pos + 1;
    advance(l); /* opening quote */
    StrBuf sb;
    sb_init(&sb);
    bool terminated = false;
    while (l->pos < l->len) {
        char c = peek(l);
        if (c == '"') {
            advance(l);
            terminated = true;
            break;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\\') {
            advance(l);
            decode_escape(l, &sb, false);
            continue;
        }
        sb_append_char(&sb, c);
        advance(l);
    }
    if (!terminated) {
        diag_error(l->diag, start, E_SYNTAX, "unterminated string literal");
    }
    size_t length = l->pos > content_start + 1 ? l->pos - content_start - 1 : 0;
    Token *token = push_token(l, TOK_STRING, start, content_start, length);
    token->str_value = arena_strdup(l->arena, sb.data ? sb.data : "");
    token->str_length = sb.len;
    sb_free(&sb);
}

static void scan_char(Lexer *l, SourcePos start) {
    size_t content_start = l->pos + 1;
    advance(l);
    StrBuf sb;
    sb_init(&sb);
    bool terminated = false;
    while (l->pos < l->len) {
        char c = peek(l);
        if (c == '\'') {
            advance(l);
            terminated = true;
            break;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\\') {
            advance(l);
            decode_escape(l, &sb, true);
            continue;
        }
        sb_append_char(&sb, c);
        advance(l);
    }
    if (!terminated) {
        diag_error(l->diag, start, E_SYNTAX, "unterminated char literal");
    } else if (sb.len != 1) {
        diag_error(l->diag, start, E_SYNTAX,
                   "char literal must contain exactly one byte (found %llu)",
                   (unsigned long long)sb.len);
    }
    size_t length = l->pos > content_start + 1 ? l->pos - content_start - 1 : 0;
    Token *token = push_token(l, TOK_CHAR, start, content_start, length);
    token->ival = sb.len >= 1 ? (int64_t)(unsigned char)sb.data[0] : 0;
    token->uval = (uint64_t)token->ival;
    sb_free(&sb);
}

static void scan_number(Lexer *l, SourcePos start) {
    size_t token_start = l->pos;
    char digits[256];
    size_t ndigits = 0;
    bool is_float = false;
    int base = 10;
    bool overflow_buffer = false;

    char c0 = peek(l);
    char c1 = peek_at(l, 1);
    if (c0 == '0' && (c1 == 'x' || c1 == 'X' || c1 == 'b' || c1 == 'B' || c1 == 'o' || c1 == 'O')) {
        if (c1 == 'x' || c1 == 'X') {
            base = 16;
        } else if (c1 == 'b' || c1 == 'B') {
            base = 2;
        } else {
            base = 8;
        }
        advance(l);
        advance(l);
        bool any = false;
        while (l->pos < l->len) {
            char c = peek(l);
            bool valid;
            if (base == 16) {
                valid = isxdigit((unsigned char)c) != 0;
            } else if (base == 8) {
                valid = c >= '0' && c <= '7';
            } else {
                valid = c == '0' || c == '1';
            }
            if (valid) {
                any = true;
                if (ndigits + 1 < sizeof(digits)) {
                    digits[ndigits++] = c;
                } else {
                    overflow_buffer = true;
                }
                advance(l);
            } else if (c == '_') {
                advance(l);
            } else {
                break;
            }
        }
        if (!any) {
            diag_error(l->diag, start, E_SYNTAX, "integer literal has no digits");
        }
        if (isalnum((unsigned char)peek(l))) {
            diag_error(l->diag, current_pos(l), E_SYNTAX,
                       "invalid digit '%c' in base-%d literal", peek(l), base);
        }
    } else {
        while (l->pos < l->len) {
            char c = peek(l);
            if (isdigit((unsigned char)c)) {
                if (ndigits + 1 < sizeof(digits)) {
                    digits[ndigits++] = c;
                } else {
                    overflow_buffer = true;
                }
                advance(l);
            } else if (c == '_') {
                advance(l);
            } else {
                break;
            }
        }
        if (peek(l) == '.' && isdigit((unsigned char)peek_at(l, 1))) {
            is_float = true;
            if (ndigits + 1 < sizeof(digits)) {
                digits[ndigits++] = '.';
            }
            advance(l);
            while (l->pos < l->len) {
                char c = peek(l);
                if (isdigit((unsigned char)c)) {
                    if (ndigits + 1 < sizeof(digits)) {
                        digits[ndigits++] = c;
                    } else {
                        overflow_buffer = true;
                    }
                    advance(l);
                } else if (c == '_') {
                    advance(l);
                } else {
                    break;
                }
            }
        }
        if (peek(l) == 'e' || peek(l) == 'E') {
            char sign = peek_at(l, 1);
            bool has_sign = sign == '+' || sign == '-';
            if (isdigit((unsigned char)peek_at(l, has_sign ? 2 : 1))) {
                is_float = true;
                if (ndigits + 1 < sizeof(digits)) {
                    digits[ndigits++] = 'e';
                }
                advance(l);
                if (has_sign) {
                    if (ndigits + 1 < sizeof(digits)) {
                        digits[ndigits++] = sign;
                    }
                    advance(l);
                }
                while (l->pos < l->len) {
                    char c = peek(l);
                    if (isdigit((unsigned char)c)) {
                        if (ndigits + 1 < sizeof(digits)) {
                            digits[ndigits++] = c;
                        } else {
                            overflow_buffer = true;
                        }
                        advance(l);
                    } else if (c == '_') {
                        advance(l);
                    } else {
                        break;
                    }
                }
            }
        }
    }
    digits[ndigits] = '\0';

    if (overflow_buffer) {
        diag_error(l->diag, start, E_LITERAL_RANGE, "numeric literal is too long");
    }

    if (is_float) {
        errno = 0;
        char *end = NULL;
        double value = strtod(digits, &end);
        if (errno == ERANGE) {
            diag_error(l->diag, start, E_LITERAL_RANGE, "floating point literal is out of range");
        }
        Token *token = push_token(l, TOK_FLOAT, start, token_start, l->pos - token_start);
        token->fval = value;
        return;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(digits, &end, base);
    if (errno == ERANGE) {
        diag_error(l->diag, start, E_LITERAL_RANGE, "integer literal does not fit in 64 bits");
    }
    Token *token = push_token(l, TOK_INT, start, token_start, l->pos - token_start);
    token->uval = (uint64_t)value;
    token->ival = (int64_t)value;
}

static void scan_identifier(Lexer *l, SourcePos start) {
    size_t token_start = l->pos;
    while (l->pos < l->len && is_ident_continue(peek(l))) {
        advance(l);
    }
    size_t length = l->pos - token_start;
    Token token;
    memset(&token, 0, sizeof(token));
    token.kind = TOK_IDENT;
    token.pos = start;
    token.lexeme = l->src + token_start;
    token.length = (int)length;
    for (size_t i = 0; i < RYN_ARRAY_LEN(KEYWORDS); i++) {
        if (strlen(KEYWORDS[i].text) == length &&
            memcmp(KEYWORDS[i].text, l->src + token_start, length) == 0) {
            token.kind = KEYWORDS[i].kind;
            break;
        }
    }
    TokenVec_push(&l->tokens, token);
}

void lexer_init(Lexer *lexer, Arena *arena, Diagnostics *diag, SourceManager *sm) {
    memset(lexer, 0, sizeof(*lexer));
    lexer->arena = arena;
    lexer->diag = diag;
    lexer->sm = sm;
    TokenVec_init(&lexer->tokens);
}

void lexer_free(Lexer *lexer) {
    TokenVec_free(&lexer->tokens);
}

static bool lex_all(Lexer *l) {
    int errors_before = l->diag->error_count;
    int indent_stack[MAX_INDENT_LEVELS];
    int indent_top = 0;
    indent_stack[0] = 0;
    Bracket brackets[MAX_BRACKETS];
    int bracket_top = 0;
    bool at_bol = true;
    int depth = 0;

    while (l->pos < l->len) {
        if (at_bol) {
            if (depth > 0) {
                /* Inside brackets: ignore layout entirely. */
                while (l->pos < l->len) {
                    char c = peek(l);
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                        advance(l);
                    } else {
                        break;
                    }
                }
                at_bol = false;
                if (l->pos >= l->len) {
                    break;
                }
            } else {
                bool tab_reported = false;
                /* Skip indentation, comments and blank lines. */
                for (;;) {
                    char c = peek(l);
                    if (c == ' ') {
                        advance(l);
                    } else if (c == '\r') {
                        advance(l);
                    } else if (c == '\t') {
                        if (!tab_reported) {
                            tab_reported = true;
                            SourcePos pos = current_pos(l);
                            diag_error(l->diag, pos, E_INDENT,
                                       "tab character in indentation; Ryn uses spaces");
                        }
                        advance(l);
                    } else if (c == '#') {
                        bool multiline = skip_comment(l);
                        if (multiline) {
                            /* Re-measure indentation from the current line. */
                            continue;
                        }
                        continue;
                    } else {
                        break;
                    }
                }
                if (l->pos >= l->len) {
                    break;
                }
                if (peek(l) == '\n') {
                    advance(l);
                    continue;
                }
                SourcePos pos = current_pos(l);
                int indent = l->column - 1;
                if (indent > indent_stack[indent_top]) {
                    if (indent_top + 1 < MAX_INDENT_LEVELS) {
                        indent_top++;
                        indent_stack[indent_top] = indent;
                    }
                    push_token(l, TOK_INDENT, pos, l->pos, 0);
                } else if (indent < indent_stack[indent_top]) {
                    while (indent_top > 0 && indent < indent_stack[indent_top]) {
                        indent_top--;
                        push_token(l, TOK_DEDENT, pos, l->pos, 0);
                    }
                    if (indent != indent_stack[indent_top]) {
                        diag_error(l->diag, pos, E_INDENT,
                                   "unindent does not match any outer indentation level");
                        indent_stack[indent_top] = indent;
                    }
                }
                at_bol = false;
            }
        }

        if (l->pos >= l->len) {
            break;
        }
        char c = peek(l);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(l);
            continue;
        }
        if (c == '\n') {
            SourcePos pos = current_pos(l);
            advance(l);
            if (depth == 0) {
                push_token(l, TOK_NEWLINE, pos, l->pos - 1, 0);
            }
            at_bol = true;
            continue;
        }
        if (c == '#') {
            skip_comment(l);
            continue;
        }

        SourcePos start = current_pos(l);
        if (is_ident_start(c)) {
            scan_identifier(l, start);
            continue;
        }
        if (isdigit((unsigned char)c)) {
            scan_number(l, start);
            continue;
        }
        if (c == '"') {
            scan_string(l, start);
            continue;
        }
        if (c == '\'') {
            scan_char(l, start);
            continue;
        }

        switch (c) {
        case '(':
        case '[':
        case '{': {
            if (bracket_top < MAX_BRACKETS) {
                brackets[bracket_top].open = c;
                brackets[bracket_top].pos = start;
                bracket_top++;
            }
            depth++;
            advance(l);
            push_token(l, c == '(' ? TOK_LPAREN : c == '[' ? TOK_LBRACKET : TOK_LBRACE, start,
                       l->pos - 1, 1);
            continue;
        }
        case ')':
        case ']':
        case '}': {
            char expected = c == ')' ? '(' : c == ']' ? '[' : '{';
            if (bracket_top == 0) {
                diag_error(l->diag, start, E_SYNTAX, "unmatched '%c'", c);
            } else {
                Bracket top = brackets[bracket_top - 1];
                if (top.open != expected) {
                    diag_error(l->diag, start, E_SYNTAX,
                               "mismatched '%c': expected to close '%c' from line %d", c, top.open,
                               top.pos.line);
                } else {
                    bracket_top--;
                    depth--;
                }
            }
            advance(l);
            push_token(l, c == ')' ? TOK_RPAREN : c == ']' ? TOK_RBRACKET : TOK_RBRACE, start,
                       l->pos - 1, 1);
            continue;
        }
        default:
            break;
        }

        /* Operators and punctuation. */
        struct Punct {
            const char *text;
            TokenKind kind;
        };
        static const struct Punct punctuation[] = {
            {"..=", TOK_DOTDOTEQ}, {"..", TOK_DOTDOT},     {"->", TOK_ARROW},
            {"==", TOK_EQ},        {"!=", TOK_NE},         {"<=", TOK_LE},
            {">=", TOK_GE},        {"&&", TOK_AMPAMP},     {"||", TOK_PIPEPIPE},
            {"<<", TOK_SHL},       {">>", TOK_SHR},        {"+=", TOK_PLUS_ASSIGN},
            {"-=", TOK_MINUS_ASSIGN}, {"*=", TOK_STAR_ASSIGN},
            {"/=", TOK_SLASH_ASSIGN}, {"%=", TOK_PERCENT_ASSIGN},
        };
        bool matched = false;
        for (size_t i = 0; i < RYN_ARRAY_LEN(punctuation); i++) {
            size_t n = strlen(punctuation[i].text);
            if (l->pos + n <= l->len && memcmp(l->src + l->pos, punctuation[i].text, n) == 0) {
                for (size_t k = 0; k < n; k++) {
                    advance(l);
                }
                push_token(l, punctuation[i].kind, start, l->pos - n, n);
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }

        TokenKind single;
        switch (c) {
        case '+': single = TOK_PLUS; break;
        case '-': single = TOK_MINUS; break;
        case '*': single = TOK_STAR; break;
        case '/': single = TOK_SLASH; break;
        case '%': single = TOK_PERCENT; break;
        case '=': single = TOK_ASSIGN; break;
        case '<': single = TOK_LT; break;
        case '>': single = TOK_GT; break;
        case '&': single = TOK_AMP; break;
        case '|': single = TOK_PIPE; break;
        case '^': single = TOK_CARET; break;
        case '~': single = TOK_TILDE; break;
        case '!': single = TOK_BANG; break;
        case ':': single = TOK_COLON; break;
        case ';': single = TOK_SEMICOLON; break;
        case ',': single = TOK_COMMA; break;
        case '.': single = TOK_DOT; break;
        default:
            diag_error(l->diag, start, E_SYNTAX, "unexpected character '\\x%02X'", (unsigned char)c);
            advance(l);
            continue;
        }
        advance(l);
        push_token(l, single, start, l->pos - 1, 1);
    }

    if (bracket_top > 0) {
        Bracket top = brackets[bracket_top - 1];
        diag_error(l->diag, top.pos, E_SYNTAX, "unclosed '%c'", top.open);
    }

    SourcePos eof = current_pos(l);
    if (l->tokens.len > 0 && l->tokens.items[l->tokens.len - 1].kind != TOK_NEWLINE) {
        push_token(l, TOK_NEWLINE, eof, l->pos, 0);
    }
    while (indent_top > 0) {
        indent_top--;
        push_token(l, TOK_DEDENT, eof, l->pos, 0);
    }
    push_token(l, TOK_EOF, eof, l->pos, 0);

    return l->diag->error_count == errors_before;
}

bool lexer_lex(Lexer *lexer, SourceFile *file) {
    lexer->file = file;
    lexer->src = file->data;
    lexer->len = file->len;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
    TokenVec_free(&lexer->tokens);
    TokenVec_init(&lexer->tokens);
    return lex_all(lexer);
}

/* ------------------------------------------------------------------ */
/* Debug dump                                                         */
/* ------------------------------------------------------------------ */

void lexer_dump_tokens(const TokenVec *tokens, FILE *out) {
    for (size_t i = 0; i < tokens->len; i++) {
        const Token *token = &tokens->items[i];
        char description[256];
        token_describe(token, description, sizeof(description));
        fprintf(out, "%4d:%-3d %-14s %s\n", token->pos.line, token->pos.column,
                token_kind_name(token->kind), description);
    }
}
