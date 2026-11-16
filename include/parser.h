/*
 * Ryn programming language — parser.
 *
 * A hand written recursive descent parser over the token stream produced by
 * the lexer. The parser recovers from errors by synchronizing at the next
 * statement boundary so that a single mistake does not produce cascades.
 */
#ifndef RYN_PARSER_H
#define RYN_PARSER_H

#include "ast.h"
#include "diagnostics.h"
#include "lexer.h"
#include "source.h"

typedef struct Parser {
    Arena *arena;
    Diagnostics *diag;
    SourceManager *sm;
    TokenVec *tokens;
    size_t index;
    bool panic_mode;
    bool fatal;
    int errors_at_start;
} Parser;

void parser_init(Parser *parser, Arena *arena, Diagnostics *diag, SourceManager *sm,
                 TokenVec *tokens);
/* Parses a whole program. Returns an AST even in the presence of errors;
 * callers must check diagnostics before using it further. */
AstProgram *parser_parse_program(Parser *parser, SourceFile *file);

#endif /* RYN_PARSER_H */
