/*
 * Shared helpers for the Ryn compiler unit tests.
 *
 * A TestCompilation bundles every stage for one source string so a test can
 * stop after lexing, parsing, semantic analysis, IR or C generation.
 */
#ifndef RYN_TEST_UTIL_H
#define RYN_TEST_UTIL_H

#include "ast.h"
#include "codegen.h"
#include "common.h"
#include "diagnostics.h"
#include "ir.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "source.h"
#include "types.h"

typedef struct TestStats {
    int passed;
    int failed;
} TestStats;

extern TestStats test_stats;

void test_fail(const char *file, int line, const char *fmt, ...);

#define TEST_ASSERT(cond)                                                              \
    do {                                                                               \
        if (cond) {                                                                    \
            test_stats.passed++;                                                       \
        } else {                                                                       \
            test_stats.failed++;                                                       \
            test_fail(__FILE__, __LINE__, "assertion failed: %s", #cond);              \
        }                                                                              \
    } while (0)

#define TEST_ASSERT_MSG(cond, ...)                                                     \
    do {                                                                               \
        if (cond) {                                                                    \
            test_stats.passed++;                                                       \
        } else {                                                                       \
            test_stats.failed++;                                                       \
            test_fail(__FILE__, __LINE__, __VA_ARGS__);                                \
        }                                                                              \
    } while (0)

#define TEST_ASSERT_INT(expected, actual)                                              \
    do {                                                                               \
        long long e_ = (long long)(expected);                                          \
        long long a_ = (long long)(actual);                                            \
        if (e_ == a_) {                                                                \
            test_stats.passed++;                                                       \
        } else {                                                                       \
            test_stats.failed++;                                                       \
            test_fail(__FILE__, __LINE__, "expected %lld, got %lld", e_, a_);          \
        }                                                                              \
    } while (0)

#define TEST_ASSERT_STR(expected, actual)                                              \
    do {                                                                               \
        const char *e_ = (expected);                                                   \
        const char *a_ = (actual);                                                     \
        if (e_ && a_ && strcmp(e_, a_) == 0) {                                         \
            test_stats.passed++;                                                       \
        } else {                                                                       \
            test_stats.failed++;                                                       \
            test_fail(__FILE__, __LINE__, "expected \"%s\", got \"%s\"", e_ ? e_ : "(null)", \
                      a_ ? a_ : "(null)");                                             \
        }                                                                              \
    } while (0)

typedef struct TestCompilation {
    Arena arena;
    SourceManager sm;
    Diagnostics diag;
    Lexer lexer;
    AstProgram *program;
    TypeContext types;
    Sema sema;
    IrModule *module;
    bool types_ready;
    bool sema_ready;
} TestCompilation;

void tc_init(TestCompilation *tc);
void tc_free(TestCompilation *tc);

bool tc_lex(TestCompilation *tc, const char *source);
bool tc_parse(TestCompilation *tc, const char *source);
bool tc_analyze(TestCompilation *tc, const char *source);
bool tc_build_ir(TestCompilation *tc, const char *source);

/* Emits C for the already analyzed program. Returns a malloc'd string, or
 * NULL when code generation failed. */
char *tc_generate_c(TestCompilation *tc);

bool tc_has_code(const TestCompilation *tc, DiagCode code);

/* Token helpers. */
bool tc_expect_tokens(TestCompilation *tc, const TokenKind *kinds, size_t count);

#endif /* RYN_TEST_UTIL_H */
