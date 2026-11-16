#include "test_util.h"

#include "module.h"

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT(file != NULL);
    if (!file) {
        return;
    }
    fputs(text, file);
    fclose(file);
}

static bool has_code(const Diagnostics *diag, DiagCode code) {
    for (size_t i = 0; i < diag->items.len; i++) {
        if (diag->items.items[i].code == code) {
            return true;
        }
    }
    return false;
}

static AstDecl *find_decl(AstProgram *program, NodeKind kind, const char *name) {
    for (size_t i = 0; i < program->decls.len; i++) {
        AstDecl *decl = program->decls.items[i];
        if (decl->kind != kind) {
            continue;
        }
        if (kind == AST_FN_DECL && strcmp(((AstFnDecl *)decl)->name, name) == 0) {
            return decl;
        }
        if (kind == AST_STRUCT_DECL && strcmp(((AstStructDecl *)decl)->name, name) == 0) {
            return decl;
        }
        if (kind == AST_ENUM_DECL && strcmp(((AstEnumDecl *)decl)->name, name) == 0) {
            return decl;
        }
    }
    return NULL;
}

typedef struct ModuleTest {
    Arena arena;
    SourceManager sm;
    Diagnostics diag;
} ModuleTest;

static void mt_init(ModuleTest *mt) {
    arena_init(&mt->arena);
    sm_init(&mt->sm, &mt->arena);
    diag_init(&mt->diag);
}

static void mt_free(ModuleTest *mt) {
    diag_free(&mt->diag);
    sm_free(&mt->sm);
    arena_free(&mt->arena);
}

static void test_local_module_import(void) {
    char *dir = ryn_make_temp_dir();
    TEST_ASSERT(dir != NULL);
    if (!dir) {
        return;
    }
    char *main_path = ryn_path_join(dir, "main.ryn");
    char *util_path = ryn_path_join(dir, "util.ryn");
    write_file(main_path,
               "import util\n"
               "\n"
               "fn main() -> i32:\n"
               "    let p = util.Point { x: 1, y: 2 }\n"
               "    return util.sum(p)\n");
    write_file(util_path,
               "struct Point:\n"
               "    x: i32\n"
               "    y: i32\n"
               "\n"
               "fn sum(p: Point) -> i32:\n"
               "    return p.x + p.y\n");

    ModuleTest mt;
    mt_init(&mt);
    AstProgram *program = NULL;
    ModuleOptions options;
    memset(&options, 0, sizeof(options));
    TEST_ASSERT(modules_load_program(&mt.arena, &mt.diag, &mt.sm, main_path, &options, false,
                                     &program));
    TEST_ASSERT(program != NULL);
    TEST_ASSERT_INT(0, mt.diag.error_count);
    if (program) {
        TEST_ASSERT_INT(3, program->decls.len);
        AstFnDecl *main_fn = (AstFnDecl *)find_decl(program, AST_FN_DECL, "main");
        TEST_ASSERT(main_fn != NULL);
        AstStructDecl *st = (AstStructDecl *)find_decl(program, AST_STRUCT_DECL, "util.Point");
        TEST_ASSERT(st != NULL);
        TEST_ASSERT(find_decl(program, AST_FN_DECL, "util.sum") != NULL);
        if (main_fn) {
            AstVarDecl *var = (AstVarDecl *)main_fn->body->stmts[0];
            TEST_ASSERT_INT(AST_STRUCT_LITERAL, var->init->kind);
            AstStructLiteral *literal = (AstStructLiteral *)var->init;
            TEST_ASSERT_STR("util.Point", literal->type_syntax->name);
            AstReturn *ret = (AstReturn *)main_fn->body->stmts[1];
            TEST_ASSERT_INT(AST_CALL, ret->value->kind);
            AstCall *call = (AstCall *)ret->value;
            TEST_ASSERT_INT(AST_IDENT, call->callee->kind);
            TEST_ASSERT_STR("util.sum", ((AstIdent *)call->callee)->name);
        }

        TypeContext types;
        types_init(&types, &mt.arena);
        Sema sema;
        sema_init(&sema, &mt.arena, &mt.diag, &mt.sm);
        TEST_ASSERT(sema_analyze(&sema, program));
        TEST_ASSERT_INT(0, mt.diag.error_count);
        sema_free(&sema);
    }

    free(main_path);
    free(util_path);
    ast_program_free(program);
    mt_free(&mt);
    ryn_remove_temp_dir(dir);
    free(dir);
}

static void test_package_import(void) {
    char *dir = ryn_make_temp_dir();
    TEST_ASSERT(dir != NULL);
    if (!dir) {
        return;
    }
    char *state = ryn_path_join(dir, ".rynx");
    char *packages = ryn_path_join(state, "packages");
    char *pkg = ryn_path_join(packages, "pkg");
    char *src = ryn_path_join(pkg, "src");
    TEST_ASSERT(ryn_make_dir(state));
    TEST_ASSERT(ryn_make_dir(packages));
    TEST_ASSERT(ryn_make_dir(pkg));
    TEST_ASSERT(ryn_make_dir(src));
    char *main_path = ryn_path_join(dir, "main.ryn");
    char *lib_path = ryn_path_join(src, "lib.ryn");
    write_file(main_path,
               "import pkg\n"
               "\n"
               "fn main() -> i32:\n"
               "    return pkg.answer()\n");
    write_file(lib_path,
               "fn answer() -> i32:\n"
               "    return 42\n");

    ModuleTest mt;
    mt_init(&mt);
    AstProgram *program = NULL;
    ModuleOptions options;
    memset(&options, 0, sizeof(options));
    TEST_ASSERT(modules_load_program(&mt.arena, &mt.diag, &mt.sm, main_path, &options, false,
                                     &program));
    TEST_ASSERT(program != NULL);
    TEST_ASSERT_INT(0, mt.diag.error_count);
    if (program) {
        TEST_ASSERT(find_decl(program, AST_FN_DECL, "pkg.answer") != NULL);
        TEST_ASSERT(find_decl(program, AST_FN_DECL, "main") != NULL);
    }

    free(main_path);
    free(lib_path);
    free(src);
    free(pkg);
    free(packages);
    free(state);
    ast_program_free(program);
    mt_free(&mt);
    ryn_remove_temp_dir(dir);
    free(dir);
}

static void test_import_cycle(void) {
    char *dir = ryn_make_temp_dir();
    TEST_ASSERT(dir != NULL);
    if (!dir) {
        return;
    }
    char *a_path = ryn_path_join(dir, "a.ryn");
    char *b_path = ryn_path_join(dir, "b.ryn");
    write_file(a_path,
               "import b\n"
               "\n"
               "fn main() -> i32:\n"
               "    return 0\n");
    write_file(b_path,
               "import a\n"
               "\n"
               "fn helper() -> i32:\n"
               "    return 1\n");

    ModuleTest mt;
    mt_init(&mt);
    AstProgram *program = NULL;
    ModuleOptions options;
    memset(&options, 0, sizeof(options));
    TEST_ASSERT(!modules_load_program(&mt.arena, &mt.diag, &mt.sm, a_path, &options, false,
                                      &program));
    TEST_ASSERT(has_code(&mt.diag, E_IMPORT_CYCLE));

    free(a_path);
    free(b_path);
    ast_program_free(program);
    mt_free(&mt);
    ryn_remove_temp_dir(dir);
    free(dir);
}

static void test_unknown_module(void) {
    char *dir = ryn_make_temp_dir();
    TEST_ASSERT(dir != NULL);
    if (!dir) {
        return;
    }
    char *main_path = ryn_path_join(dir, "main.ryn");
    write_file(main_path,
               "import nowhere\n"
               "\n"
               "fn main() -> i32:\n"
               "    return 0\n");

    ModuleTest mt;
    mt_init(&mt);
    AstProgram *program = NULL;
    ModuleOptions options;
    memset(&options, 0, sizeof(options));
    TEST_ASSERT(!modules_load_program(&mt.arena, &mt.diag, &mt.sm, main_path, &options, false,
                                      &program));
    TEST_ASSERT(has_code(&mt.diag, E_UNKNOWN_MODULE));

    free(main_path);
    ast_program_free(program);
    mt_free(&mt);
    ryn_remove_temp_dir(dir);
    free(dir);
}

static void test_module_path_option(void) {
    char *dir = ryn_make_temp_dir();
    TEST_ASSERT(dir != NULL);
    if (!dir) {
        return;
    }
    char *modules = ryn_path_join(dir, "modules");
    char *pkg = ryn_path_join(modules, "pkg");
    char *src = ryn_path_join(pkg, "src");
    TEST_ASSERT(ryn_make_dir(modules));
    TEST_ASSERT(ryn_make_dir(pkg));
    TEST_ASSERT(ryn_make_dir(src));
    char *main_path = ryn_path_join(dir, "main.ryn");
    char *lib_path = ryn_path_join(src, "lib.ryn");
    write_file(main_path,
               "import pkg\n"
               "\n"
               "fn main() -> i32:\n"
               "    return pkg.answer()\n");
    write_file(lib_path,
               "fn answer() -> i32:\n"
               "    return 7\n");

    ModuleTest mt;
    mt_init(&mt);
    AstProgram *program = NULL;
    const char *paths[] = {modules};
    ModuleOptions options;
    options.search_paths = paths;
    options.nsearch_paths = 1;
    TEST_ASSERT(modules_load_program(&mt.arena, &mt.diag, &mt.sm, main_path, &options, false,
                                     &program));
    TEST_ASSERT(program != NULL);
    TEST_ASSERT_INT(0, mt.diag.error_count);
    if (program) {
        TEST_ASSERT(find_decl(program, AST_FN_DECL, "pkg.answer") != NULL);
    }

    free(main_path);
    free(lib_path);
    free(src);
    free(pkg);
    free(modules);
    ast_program_free(program);
    mt_free(&mt);
    ryn_remove_temp_dir(dir);
    free(dir);
}

void run_module_tests(void) {
    test_local_module_import();
    test_package_import();
    test_import_cycle();
    test_unknown_module();
    test_module_path_option();
}
