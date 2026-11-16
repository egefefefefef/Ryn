#include "test_util.h"

static char *generate(const char *source) {
    TestCompilation tc;
    tc_init(&tc);
    if (!tc_build_ir(&tc, source)) {
        fprintf(stderr, "codegen test input failed to compile\n");
        tc_free(&tc);
        return NULL;
    }
    char *c_source = tc_generate_c(&tc);
    tc_free(&tc);
    return c_source;
}

static void test_hello_world(void) {
    char *c = generate("fn main() -> i32:\n"
                       "    println(\"Hello, Ryn!\")\n"
                       "    return 0\n");
    TEST_ASSERT(c != NULL);
    if (c) {
        TEST_ASSERT(strstr(c, "int main(void)") != NULL);
        TEST_ASSERT(strstr(c, "int32_t ryn_main(void)") != NULL);
        TEST_ASSERT(strstr(c, "ryn_println_str") != NULL);
        TEST_ASSERT(strstr(c, "static const char ryn_str_0[]") != NULL);
        TEST_ASSERT(strstr(c, "#include \"ryn_runtime.h\"") != NULL);
        TEST_ASSERT(strstr(c, "goto bb0;") != NULL);
    }
    free(c);
}

static void test_struct_field_access(void) {
    char *c = generate("struct Point:\n"
                       "    x: i32\n"
                       "    y: i32\n"
                       "\n"
                       "fn main() -> i32:\n"
                       "    let p = Point { x: 1, y: 2 }\n"
                       "    return p.x\n");
    TEST_ASSERT(c != NULL);
    if (c) {
        TEST_ASSERT(strstr(c, "struct ryn_s_Point") != NULL);
        TEST_ASSERT(strstr(c, "f_x") != NULL);
        TEST_ASSERT(strstr(c, "->f_x") != NULL);
        TEST_ASSERT(strstr(c, "memcpy") != NULL);
    }
    free(c);
}

static void test_checked_division(void) {
    char *c = generate("fn main() -> i32:\n"
                       "    var d: i32 = 2\n"
                       "    return 10 / d\n");
    TEST_ASSERT(c != NULL);
    if (c) {
        TEST_ASSERT(strstr(c, "ryn_panic_div_zero") != NULL);
    }
    free(c);
}

static void test_control_flow_blocks(void) {
    char *c = generate("fn main() -> i32:\n"
                       "    var i: i32 = 0\n"
                       "    while i < 3:\n"
                       "        i += 1\n"
                       "    if i == 3:\n"
                       "        return 1\n"
                       "    return 0\n");
    TEST_ASSERT(c != NULL);
    if (c) {
        TEST_ASSERT(strstr(c, "goto bb") != NULL);
        TEST_ASSERT(strstr(c, "if (") != NULL);
    }
    free(c);
}

static void test_array_locals_and_indexing(void) {
    char *c = generate("fn main() -> i32:\n"
                       "    let a: [i32; 3] = [1, 2, 3]\n"
                       "    return a[1]\n");
    TEST_ASSERT(c != NULL);
    if (c) {
        TEST_ASSERT(strstr(c, "[3]") != NULL);
        TEST_ASSERT(strstr(c, "memcpy") != NULL);
    }
    free(c);
}

static void test_void_main_wrapper(void) {
    char *c = generate("fn main():\n"
                       "    println(\"void\")\n");
    TEST_ASSERT(c != NULL);
    if (c) {
        TEST_ASSERT(strstr(c, "void ryn_main(void)") != NULL);
        TEST_ASSERT(strstr(c, "ryn_main();") != NULL);
        TEST_ASSERT(strstr(c, "return 0;") != NULL);
    }
    free(c);
}

static void test_cast_emits_c_cast(void) {
    char *c = generate("fn main() -> i32:\n"
                       "    let a: i32 = 300\n"
                       "    let b: u8 = a as u8\n"
                       "    return b as i32\n");
    TEST_ASSERT(c != NULL);
    if (c) {
        TEST_ASSERT(strstr(c, "(uint8_t)") != NULL);
    }
    free(c);
}

static void test_function_values(void) {
    char *c = generate("fn square(x: i32) -> i32:\n"
                       "    return x * x\n"
                       "\n"
                       "fn apply(f: fn(i32) -> i32, v: i32) -> i32:\n"
                       "    return f(v)\n"
                       "\n"
                       "struct Ops:\n"
                       "    f: fn(i32) -> i32\n"
                       "\n"
                       "fn main() -> i32:\n"
                       "    let ops = Ops { f: square }\n"
                       "    return apply(ops.f, 3)\n");
    TEST_ASSERT(c != NULL);
    if (c) {
        TEST_ASSERT(strstr(c, "typedef int32_t (*ryn_fn_0)(int32_t)") != NULL);
        TEST_ASSERT(strstr(c, "ryn_fn_0 f_f") != NULL);
    }
    free(c);
}

void run_codegen_tests(void) {
    test_hello_world();
    test_struct_field_access();
    test_checked_division();
    test_control_flow_blocks();
    test_array_locals_and_indexing();
    test_void_main_wrapper();
    test_cast_emits_c_cast();
    test_function_values();
}
