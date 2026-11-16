#include "test_util.h"

void run_lexer_tests(void);
void run_parser_tests(void);
void run_sema_tests(void);
void run_codegen_tests(void);

int main(void) {
    run_lexer_tests();
    run_parser_tests();
    run_sema_tests();
    run_codegen_tests();

    printf("unit tests: %d passed, %d failed\n", test_stats.passed, test_stats.failed);
    return test_stats.failed == 0 ? 0 : 1;
}
