#include "test_util.h"

static const char *FUNCTION_SOURCE =
    "fn add(a: i32, b: i32) -> i32:\n"
    "    return a + b\n";

static void test_function_declaration(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_parse(&tc, FUNCTION_SOURCE));
    TEST_ASSERT(tc.program != NULL);
    TEST_ASSERT_INT(1, tc.program->decls.len);
    AstDecl *decl = tc.program->decls.items[0];
    TEST_ASSERT_INT(AST_FN_DECL, decl->kind);
    AstFnDecl *fn = (AstFnDecl *)decl;
    TEST_ASSERT_STR("add", fn->name);
    TEST_ASSERT_INT(2, fn->nparams);
    TEST_ASSERT_STR("a", fn->params[0].name);
    TEST_ASSERT(fn->return_type_syntax != NULL);
    TEST_ASSERT_STR("i32", fn->return_type_syntax->name);
    TEST_ASSERT(fn->body != NULL);
    TEST_ASSERT_INT(1, fn->body->count);
    TEST_ASSERT_INT(AST_RETURN, fn->body->stmts[0]->kind);
    AstReturn *ret = (AstReturn *)fn->body->stmts[0];
    TEST_ASSERT_INT(AST_BINARY, ret->value->kind);
    tc_free(&tc);
}

static void test_operator_precedence(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_parse(&tc,
                         "fn main() -> i32:\n"
                         "    let x = 1 + 2 * 3\n"
                         "    return 0\n"));
    AstFnDecl *fn = (AstFnDecl *)tc.program->decls.items[0];
    AstVarDecl *var = (AstVarDecl *)fn->body->stmts[0];
    TEST_ASSERT_INT(AST_BINARY, var->init->kind);
    AstBinary *add = (AstBinary *)var->init;
    TEST_ASSERT_INT(BIN_ADD, add->op);
    TEST_ASSERT_INT(AST_BINARY, add->rhs->kind);
    TEST_ASSERT_INT(BIN_MUL, ((AstBinary *)add->rhs)->op);
    tc_free(&tc);
}

static void test_logical_operators(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_parse(&tc,
                         "fn main() -> i32:\n"
                         "    let b = 1 < 2 and 3 > 2 or false\n"
                         "    return 0\n"));
    AstFnDecl *fn = (AstFnDecl *)tc.program->decls.items[0];
    AstVarDecl *var = (AstVarDecl *)fn->body->stmts[0];
    AstBinary *or_expr = (AstBinary *)var->init;
    TEST_ASSERT_INT(BIN_LOR, or_expr->op);
    AstBinary *and_expr = (AstBinary *)or_expr->lhs;
    TEST_ASSERT_INT(BIN_LAND, and_expr->op);
    TEST_ASSERT_INT(BIN_LT, ((AstBinary *)and_expr->lhs)->op);
    tc_free(&tc);
}

static void test_if_else_chain(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_parse(&tc,
                         "fn main() -> i32:\n"
                         "    var x: i32 = 1\n"
                         "    if x == 1:\n"
                         "        x = 2\n"
                         "    else if x == 2:\n"
                         "        x = 3\n"
                         "    else:\n"
                         "        x = 4\n"
                         "    return x\n"));
    AstFnDecl *fn = (AstFnDecl *)tc.program->decls.items[0];
    AstIf *outer = (AstIf *)fn->body->stmts[1];
    TEST_ASSERT_INT(AST_IF, outer->base.kind);
    TEST_ASSERT(outer->else_branch != NULL);
    TEST_ASSERT_INT(AST_IF, outer->else_branch->kind);
    AstIf *inner = (AstIf *)outer->else_branch;
    TEST_ASSERT(inner->else_branch != NULL);
    TEST_ASSERT_INT(AST_BLOCK_STMT, inner->else_branch->kind);
    tc_free(&tc);
}

static void test_for_ranges(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_parse(&tc,
                         "fn main() -> i32:\n"
                         "    var n: i32 = 0\n"
                         "    for i in 0..4:\n"
                         "        n += i\n"
                         "    for j in 0..=4:\n"
                         "        n += j\n"
                         "    return n\n"));
    AstFnDecl *fn = (AstFnDecl *)tc.program->decls.items[0];
    AstFor *exclusive = (AstFor *)fn->body->stmts[1];
    AstFor *inclusive = (AstFor *)fn->body->stmts[2];
    TEST_ASSERT_STR("i", exclusive->var);
    TEST_ASSERT(!exclusive->inclusive);
    TEST_ASSERT(inclusive->inclusive);
    tc_free(&tc);
}

static void test_struct_declaration_and_literal(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_parse(&tc,
                         "struct Point:\n"
                         "    x: i32\n"
                         "    y: i32\n"
                         "\n"
                         "fn main() -> i32:\n"
                         "    let p = Point { x: 1, y: 2 }\n"
                         "    return p.x\n"));
    TEST_ASSERT_INT(2, tc.program->decls.len);
    AstStructDecl *st = (AstStructDecl *)tc.program->decls.items[0];
    TEST_ASSERT_INT(2, st->nfields);
    TEST_ASSERT_STR("x", st->fields[0].name);
    AstFnDecl *fn = (AstFnDecl *)tc.program->decls.items[1];
    AstVarDecl *var = (AstVarDecl *)fn->body->stmts[0];
    TEST_ASSERT_INT(AST_STRUCT_LITERAL, var->init->kind);
    AstStructLiteral *literal = (AstStructLiteral *)var->init;
    TEST_ASSERT_INT(2, literal->count);
    TEST_ASSERT_STR("x", literal->field_names[0]);
    TEST_ASSERT_STR("y", literal->field_names[1]);
    AstReturn *ret = (AstReturn *)fn->body->stmts[1];
    TEST_ASSERT_INT(AST_FIELD, ret->value->kind);
    tc_free(&tc);
}

static void test_array_literals(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_parse(&tc,
                         "fn main() -> i32:\n"
                         "    let a = [1, 2, 3]\n"
                         "    let b = [0; 4]\n"
                         "    return a[0]\n"));
    AstFnDecl *fn = (AstFnDecl *)tc.program->decls.items[0];
    AstArrayLiteral *list = (AstArrayLiteral *)((AstVarDecl *)fn->body->stmts[0])->init;
    TEST_ASSERT_INT(3, list->count);
    TEST_ASSERT(!list->is_repeat);
    AstArrayLiteral *repeat = (AstArrayLiteral *)((AstVarDecl *)fn->body->stmts[1])->init;
    TEST_ASSERT(repeat->is_repeat);
    TEST_ASSERT_INT(4, repeat->repeat_count);
    TEST_ASSERT_INT(AST_INDEX, ((AstReturn *)fn->body->stmts[2])->value->kind);
    tc_free(&tc);
}

static void test_cast_binds_tighter_than_addition(void) {
    TestCompilation tc;
    tc_init(&tc);
    TEST_ASSERT(tc_parse(&tc,
                         "fn main() -> i32:\n"
                         "    let a: i32 = 1\n"
                         "    let b: i64 = a as i64 + 2\n"
                         "    return 0\n"));
    AstFnDecl *fn = (AstFnDecl *)tc.program->decls.items[0];
    AstVarDecl *var = (AstVarDecl *)fn->body->stmts[1];
    AstBinary *add = (AstBinary *)var->init;
    TEST_ASSERT_INT(BIN_ADD, add->op);
    TEST_ASSERT_INT(AST_CAST, add->lhs->kind);
    tc_free(&tc);
}

static void test_syntax_error_is_reported(void) {
    TestCompilation tc;
    tc_init(&tc);
    bool parsed = tc_parse(&tc, "fn main() -> i32\n    return 0\n");
    TEST_ASSERT(!parsed);
    TEST_ASSERT(tc.program != NULL);
    TEST_ASSERT(tc_has_code(&tc, E_SYNTAX));
    tc_free(&tc);
}

void run_parser_tests(void) {
    test_function_declaration();
    test_operator_precedence();
    test_logical_operators();
    test_if_else_chain();
    test_for_ranges();
    test_struct_declaration_and_literal();
    test_array_literals();
    test_cast_binds_tighter_than_addition();
    test_syntax_error_is_reported();
}
