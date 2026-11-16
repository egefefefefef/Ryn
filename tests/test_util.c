#include "test_util.h"

TestStats test_stats;

void test_fail(const char *file, int line, const char *fmt, ...) {
    fprintf(stderr, "FAIL %s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void tc_init(TestCompilation *tc) {
    memset(tc, 0, sizeof(*tc));
    arena_init(&tc->arena);
    sm_init(&tc->sm, &tc->arena);
    diag_init(&tc->diag);
    lexer_init(&tc->lexer, &tc->arena, &tc->diag, &tc->sm);
}

void tc_free(TestCompilation *tc) {
    if (tc->module) {
        ir_module_free(tc->module);
    }
    if (tc->program) {
        ast_program_free(tc->program);
    }
    if (tc->sema_ready) {
        sema_free(&tc->sema);
    }
    lexer_free(&tc->lexer);
    diag_free(&tc->diag);
    sm_free(&tc->sm);
    arena_free(&tc->arena);
}

bool tc_lex(TestCompilation *tc, const char *source) {
    char *copy = ryn_strdup(source);
    size_t length = strlen(source);
    SourceFile *file = sm_add(&tc->sm, "test.ryn", copy, length);
    return lexer_lex(&tc->lexer, file);
}

bool tc_parse(TestCompilation *tc, const char *source) {
    if (!tc_lex(tc, source)) {
        return false;
    }
    Parser parser;
    parser_init(&parser, &tc->arena, &tc->diag, &tc->sm, &tc->lexer.tokens);
    tc->program = parser_parse_program(&parser, sm_main_file(&tc->sm));
    return tc->program != NULL && tc->diag.error_count == 0;
}

bool tc_analyze(TestCompilation *tc, const char *source) {
    if (!tc->program) {
        if (!tc_parse(tc, source)) {
            return false;
        }
    }
    if (!tc->types_ready) {
        types_init(&tc->types, &tc->arena);
        tc->types_ready = true;
    }
    if (!tc->sema_ready) {
        sema_init(&tc->sema, &tc->arena, &tc->diag, &tc->sm);
        tc->sema_ready = true;
    }
    bool ok = sema_analyze(&tc->sema, tc->program);
    return ok && tc->diag.error_count == 0;
}

bool tc_build_ir(TestCompilation *tc, const char *source) {
    if (!tc->program && !tc->sema_ready) {
        if (!tc_analyze(tc, source)) {
            return false;
        }
    }
    if (tc->module) {
        return true;
    }
    tc->module = ir_build(&tc->arena, &tc->diag, tc->program, &tc->types);
    return tc->module != NULL && tc->diag.error_count == 0;
}

char *tc_generate_c(TestCompilation *tc) {
    FILE *out = tmpfile();
    if (!out) {
        return NULL;
    }
    CodegenOptions options;
    memset(&options, 0, sizeof(options));
    options.source_path = "test.ryn";
    bool ok = codegen_emit(out, &tc->arena, &tc->diag, tc->program, tc->module, &tc->types,
                           &options);
    if (!ok || tc->diag.error_count > 0) {
        fclose(out);
        return NULL;
    }
    long size = ftell(out);
    if (size < 0) {
        fclose(out);
        return NULL;
    }
    rewind(out);
    char *buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(out);
        return NULL;
    }
    size_t read = fread(buffer, 1, (size_t)size, out);
    buffer[read] = '\0';
    fclose(out);
    return buffer;
}

bool tc_has_code(const TestCompilation *tc, DiagCode code) {
    for (size_t i = 0; i < tc->diag.items.len; i++) {
        if (tc->diag.items.items[i].code == code) {
            return true;
        }
    }
    return false;
}

bool tc_expect_tokens(TestCompilation *tc, const TokenKind *kinds, size_t count) {
    if (tc->lexer.tokens.len != count) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (tc->lexer.tokens.items[i].kind != kinds[i]) {
            return false;
        }
    }
    return true;
}
