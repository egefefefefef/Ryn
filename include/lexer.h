/*
 * Ryn programming language — lexer.
 *
 * Converts a source file into tokens, inserting TOK_INDENT / TOK_DEDENT
 * tokens based on leading spaces. Rules (see docs/syntax.md):
 *   - indentation uses spaces only; tabs in indentation are diagnosed;
 *   - blank lines and comment-only lines produce no tokens;
 *   - newlines inside () and [] do not terminate statements;
 *   - a file ends with a single TOK_NEWLINE (if needed), the matching
 *     TOK_DEDENT tokens and TOK_EOF.
 */
#ifndef RYN_LEXER_H
#define RYN_LEXER_H

#include "common.h"
#include "diagnostics.h"
#include "source.h"
#include "token.h"

typedef struct Lexer {
    Arena *arena;
    Diagnostics *diag;
    SourceManager *sm;

    SourceFile *file;
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int column;

    TokenVec tokens;
} Lexer;

void lexer_init(Lexer *lexer, Arena *arena, Diagnostics *diag, SourceManager *sm);
void lexer_free(Lexer *lexer);

/* Lexes the file, appending tokens to lexer->tokens (which is cleared first).
 * Returns false if lexical errors were reported. */
bool lexer_lex(Lexer *lexer, SourceFile *file);

/* Debug helper used by `ryn check --dump-tokens`. */
void lexer_dump_tokens(const TokenVec *tokens, FILE *out);

#endif /* RYN_LEXER_H */
