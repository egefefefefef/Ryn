#include "test_util.h"

static const char *VALID_PROGRAM =
    "import math\n"
    "\n"
    "struct Point:\n"
    "    x: i32\n"
    "    y: i32\n"
    "\n"
    "fn length_sq(a: Point, b: Point) -> i32:\n"
    "    let dx: i32 = b.x - a.x\n"
    "    let dy: i32 = b.y - a.y\n"
    "    return dx * dx + dy * dy\n"
    "\n"
    "fn main() -> i32:\n"
    "    let p = Point { x: 1, y: 2 }\n"
    "    let q = Point { x: 4, y: 6 }\n"
    "    let d = length_sq(p, q)\n"
    "    if d == 25:\n"
    "        println(math.sqrt(25.0))\n"
    "    else:\n"
    "        println(\"unexpected\")\n"
    "    var i: i32 = 0\n"
    "    while i < 3:\n"
    "        i += 1\n"
    "    for j in 0..3:\n"
    "        i += j\n"
    "    return i\n";

static void expect_error(const char *source, DiagCode code, const char *label) {
    TestCompilation tc;
    tc_init(&tc);
    bool ok = tc_analyze(&tc, source);
    TEST_ASSERT_MSG(!ok, "expected analysis to fail: %s", label);
    TEST_ASSERT_MSG(tc_has_code(&tc, code), "expected diagnostic code for: %s", label);
    tc_free(&tc);
}

static void test_valid_program_has_no_diagnostics(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool ok = tc_analyze(&tc, VALID_PROGRAM);
    TEST_ASSERT_MSG(ok, "valid program should analyze cleanly (%d errors, %d warnings)",
                    tc.diag.error_count, tc.diag.warning_count);
    TEST_ASSERT_INT(0, tc.diag.error_count);
    TEST_ASSERT_INT(0, tc.diag.warning_count);
    tc_free(&tc);
}

static void test_type_mismatch(void) {
    expect_error("fn main() -> i32:\n"
                 "    let x: i32 = \"text\"\n"
                 "    return 0\n",
                 E_TYPE_MISMATCH, "string assigned to i32");
}

static void test_immutable_assignment(void) {
    expect_error("fn main() -> i32:\n"
                 "    let x = 1\n"
                 "    x = 2\n"
                 "    return x\n",
                 E_IMMUTABLE_ASSIGN, "assignment to let");
}

static void test_wrong_argument_count(void) {
    expect_error("fn f(a: i32) -> i32:\n"
                 "    return a\n"
                 "\n"
                 "fn main() -> i32:\n"
                 "    return f()\n",
                 E_ARG_COUNT, "missing argument");
}

static void test_undefined_function(void) {
    expect_error("fn main() -> i32:\n"
                 "    return missing()\n",
                 E_UNDEFINED_FUNCTION, "call to an unknown function");
}

static void test_unknown_field(void) {
    expect_error("struct Point:\n"
                 "    x: i32\n"
                 "\n"
                 "fn main() -> i32:\n"
                 "    let p = Point { x: 1 }\n"
                 "    return p.z\n",
                 E_UNKNOWN_FIELD, "field that does not exist");
}

static void test_break_outside_loop(void) {
    expect_error("fn main() -> i32:\n"
                 "    break\n"
                 "    return 0\n",
                 E_BREAK_OUTSIDE_LOOP, "break outside a loop");
}

static void test_missing_main(void) {
    expect_error("fn helper() -> i32:\n"
                 "    return 1\n",
                 E_MISSING_MAIN, "no main function");
}

static void test_uninitialized_variable(void) {
    expect_error("fn main() -> i32:\n"
                 "    let x: i32\n"
                 "    println(x)\n"
                 "    return 0\n",
                 E_UNINITIALIZED, "read of an uninitialized variable");
}

static void test_duplicate_variable(void) {
    expect_error("fn main() -> i32:\n"
                 "    let x = 1\n"
                 "    let x = 2\n"
                 "    return x\n",
                 E_DUPLICATE, "duplicate declaration in one scope");
}

static void test_address_of_immutable(void) {
    expect_error("fn main() -> i32:\n"
                 "    let x = 1\n"
                 "    let p = &x\n"
                 "    return 0\n",
                 E_INVALID_ADDRESS_OF, "mutable pointer to let");
}

static void test_unused_variable_warning_is_not_an_error(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool ok = tc_analyze(&tc, "fn main() -> i32:\n"
                              "    let unused = 1\n"
                              "    return 0\n");
    TEST_ASSERT(ok);
    TEST_ASSERT_INT(0, tc.diag.error_count);
    TEST_ASSERT(tc_has_code(&tc, W_UNUSED_VARIABLE));
    tc_free(&tc);
}

static void test_widening_is_allowed_but_narrowing_is_not(void) {
    TestCompilation wide;
    tc_init(&wide);
    TEST_ASSERT(tc_analyze(&wide, "fn main() -> i32:\n"
                                  "    let small: i32 = 5\n"
                                  "    let big: i64 = small\n"
                                  "    return 0\n"));
    tc_free(&wide);

    expect_error("fn main() -> i32:\n"
                 "    let big: i64 = 5\n"
                 "    let small: i32 = big\n"
                 "    return 0\n",
                 E_TYPE_MISMATCH, "implicit narrowing");
}

static void test_wrong_main_signature(void) {
    expect_error("fn main(x: i32) -> i32:\n"
                 "    return 0\n",
                 E_BAD_MAIN, "main with parameters");
}

static void test_string_helpers_and_len(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool ok = tc_analyze(&tc, "import strings\n"
                              "\n"
                              "fn main() -> i32:\n"
                              "    println(len(\"abc\"))\n"
                              "    let joined = strings.concat(\"a\", \"b\")\n"
                              "    println(joined)\n"
                              "    strings.free(joined)\n"
                              "    return 0\n");
    TEST_ASSERT_MSG(ok, "string helpers should analyze cleanly");
    TEST_ASSERT_INT(0, tc.diag.error_count);
    tc_free(&tc);
}

static void test_negative_literals(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool ok = tc_analyze(&tc, "fn main() -> i32:\n"
                              "    let a: i8 = -128\n"
                              "    let b: i64 = -9223372036854775808\n"
                              "    println(a)\n"
                              "    println(b)\n"
                              "    return 0\n");
    TEST_ASSERT_MSG(ok, "minimum signed literals should be accepted");
    TEST_ASSERT_INT(0, tc.diag.error_count);
    tc_free(&tc);
}

static void test_function_values(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool ok = tc_analyze(&tc, "fn square(x: i32) -> i32:\n"
                              "    return x * x\n"
                              "\n"
                              "fn apply(f: fn(i32) -> i32, v: i32) -> i32:\n"
                              "    return f(v)\n"
                              "\n"
                              "fn main() -> i32:\n"
                              "    let callback: fn(i32) -> i32 = square\n"
                              "    return apply(callback, 3)\n");
    TEST_ASSERT(ok);
    tc_free(&tc);
}

static void test_sema_annotates_types(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_analyze(&tc, VALID_PROGRAM));
    AstFnDecl *main_fn = NULL;
    for (size_t i = 0; i < tc.program->decls.len; i++) {
        AstDecl *decl = tc.program->decls.items[i];
        if (decl->kind == AST_FN_DECL && strcmp(((AstFnDecl *)decl)->name, "main") == 0) {
            main_fn = (AstFnDecl *)decl;
            break;
        }
    }
    TEST_ASSERT(main_fn != NULL);
    if (!main_fn) {
        tc_free(&tc);
        return;
    }
    AstVarDecl *decl = (AstVarDecl *)main_fn->body->stmts[0];
    TEST_ASSERT(decl->symbol != NULL);
    TEST_ASSERT(decl->symbol->type != NULL);
    TEST_ASSERT_INT(TY_STRUCT, decl->symbol->type->kind);
    tc_free(&tc);
}

void run_sema_tests(void) {
    test_valid_program_has_no_diagnostics();
    test_type_mismatch();
    test_immutable_assignment();
    test_wrong_argument_count();
    test_undefined_function();
    test_unknown_field();
    test_break_outside_loop();
    test_missing_main();
    test_uninitialized_variable();
    test_duplicate_variable();
    test_address_of_immutable();
    test_unused_variable_warning_is_not_an_error();
    test_widening_is_allowed_but_narrowing_is_not();
    test_wrong_main_signature();
    test_string_helpers_and_len();
    test_function_values();
    test_negative_literals();
    test_sema_annotates_types();
}
