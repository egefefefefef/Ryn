#include "test_util.h"

static void test_simple_declaration(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool lexed = tc_lex(&tc, "let x: i32 = 42\n");
    TEST_ASSERT(lexed);
    const TokenKind expected[] = {
        TOK_KW_LET, TOK_IDENT, TOK_COLON, TOK_KW_I32,
        TOK_ASSIGN, TOK_INT, TOK_NEWLINE, TOK_EOF,
    };
    TEST_ASSERT(tc_expect_tokens(&tc, expected, RYN_ARRAY_LEN(expected)));
    TEST_ASSERT_INT(42, tc.lexer.tokens.items[5].ival);
    tc_free(&tc);
}

static void test_indentation(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool lexed = tc_lex(&tc, "fn f() -> i32:\n    return 1\n");
    TEST_ASSERT(lexed);
    const TokenKind expected[] = {
        TOK_KW_FN,    TOK_IDENT,  TOK_LPAREN, TOK_RPAREN, TOK_ARROW,
        TOK_KW_I32,   TOK_COLON,  TOK_NEWLINE, TOK_INDENT, TOK_KW_RETURN,
        TOK_INT,      TOK_NEWLINE, TOK_DEDENT, TOK_EOF,
    };
    TEST_ASSERT(tc_expect_tokens(&tc, expected, RYN_ARRAY_LEN(expected)));
    tc_free(&tc);
}

static void test_comments_and_blank_lines(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool lexed = tc_lex(&tc, "# hi\n\nlet x = 1 # trailing\n");
    TEST_ASSERT(lexed);
    const TokenKind expected[] = {
        TOK_KW_LET, TOK_IDENT, TOK_ASSIGN, TOK_INT, TOK_NEWLINE, TOK_EOF,
    };
    TEST_ASSERT(tc_expect_tokens(&tc, expected, RYN_ARRAY_LEN(expected)));
    tc_free(&tc);
}

static void test_string_escapes(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool lexed = tc_lex(&tc, "fn main() -> i32:\n    let s = \"a\\nb\"\n    return 0\n");
    TEST_ASSERT(lexed);
    const Token *string = NULL;
    for (size_t i = 0; i < tc.lexer.tokens.len; i++) {
        if (tc.lexer.tokens.items[i].kind == TOK_STRING) {
            string = &tc.lexer.tokens.items[i];
            break;
        }
    }
    TEST_ASSERT(string != NULL);
    if (string) {
        TEST_ASSERT_INT(3, string->str_length);
        TEST_ASSERT_INT('a', (unsigned char)string->str_value[0]);
        TEST_ASSERT_INT('\n', (unsigned char)string->str_value[1]);
        TEST_ASSERT_INT('b', (unsigned char)string->str_value[2]);
    }
    tc_free(&tc);
}

static void test_char_literal(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool lexed = tc_lex(&tc, "'A'");
    TEST_ASSERT(lexed);
    TEST_ASSERT(tc.lexer.tokens.len >= 1);
    TEST_ASSERT_INT(TOK_CHAR, tc.lexer.tokens.items[0].kind);
    TEST_ASSERT_INT(65, tc.lexer.tokens.items[0].ival);
    tc_free(&tc);
}

static void test_range_operators(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool lexed = tc_lex(&tc, "let x = 1..=3\n");
    TEST_ASSERT(lexed);
    TEST_ASSERT(tc.lexer.tokens.len == 8);
    TEST_ASSERT_INT(TOK_DOTDOTEQ, tc.lexer.tokens.items[4].kind);
    tc_free(&tc);
}

static void test_tab_indentation_is_an_error(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool lexed = tc_lex(&tc, "fn f() -> i32:\n\treturn 1\n");
    TEST_ASSERT(!lexed);
    TEST_ASSERT(tc_has_code(&tc, E_INDENT));
    tc_free(&tc);
}

static void test_unterminated_string_is_an_error(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool lexed = tc_lex(&tc, "fn main() -> i32:\n    let s = \"abc\n    return 0\n");
    TEST_ASSERT(!lexed);
    TEST_ASSERT(tc.diag.error_count >= 1);
    tc_free(&tc);
}

void run_lexer_tests(void) {
    test_simple_declaration();
    test_indentation();
    test_comments_and_blank_lines();
    test_string_escapes();
    test_char_literal();
    test_range_operators();
    test_tab_indentation_is_an_error();
    test_unterminated_string_is_an_error();
}
