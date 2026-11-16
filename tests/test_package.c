#include "package.h"
#include "test_util.h"

static const char *VALID_MANIFEST =
    "# demo package\n"
    "[package]\n"
    "name = \"demo\"\n"
    "version = \"0.1.0\"\n"
    "entry = \"src/main.ryn\"\n"
    "\n"
    "[dependencies]\n"
    "greet = \"../greet\"\n"
    "utils = { git = \"https://example.com/utils.git\", tag = \"v1.0.0\" }\n"
    "helper = { path = \"../helper\" }\n";

static void test_manifest_parse_valid(void) {
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[256] = {0};
    bool ok = rynx_manifest_parse(VALID_MANIFEST, "test", &arena, &manifest, err, sizeof(err));
    TEST_ASSERT_MSG(ok, "valid manifest was rejected: %s", err);
    TEST_ASSERT_STR("demo", manifest.name);
    TEST_ASSERT_STR("0.1.0", manifest.version);
    TEST_ASSERT_STR("src/main.ryn", manifest.entry);
    TEST_ASSERT_INT(3, manifest.deps.len);

    const RynxDep *greet = rynx_manifest_find_dep(&manifest, "greet");
    TEST_ASSERT(greet != NULL);
    if (greet) {
        TEST_ASSERT_INT(RYNX_DEP_PATH, greet->kind);
        TEST_ASSERT_STR("../greet", greet->path);
    }
    const RynxDep *utils = rynx_manifest_find_dep(&manifest, "utils");
    TEST_ASSERT(utils != NULL);
    if (utils) {
        TEST_ASSERT_INT(RYNX_DEP_GIT, utils->kind);
        TEST_ASSERT_STR("https://example.com/utils.git", utils->git);
        TEST_ASSERT_STR("v1.0.0", utils->ref);
    }
    rynx_manifest_free(&manifest);
    arena_free(&arena);
}

static void test_manifest_default_entry(void) {
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[256] = {0};
    const char *text = "[package]\nname = \"tiny\"\nversion = \"1.2.3\"\n";
    bool ok = rynx_manifest_parse(text, "test", &arena, &manifest, err, sizeof(err));
    TEST_ASSERT_MSG(ok, "default entry manifest was rejected: %s", err);
    TEST_ASSERT_STR(RYNX_DEFAULT_ENTRY, manifest.entry);
    TEST_ASSERT_INT(0, manifest.deps.len);
    rynx_manifest_free(&manifest);
    arena_free(&arena);
}

static void expect_invalid(const char *text, const char *label) {
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[256] = {0};
    bool ok = rynx_manifest_parse(text, "test", &arena, &manifest, err, sizeof(err));
    TEST_ASSERT_MSG(!ok, "expected a parse error: %s", label);
    TEST_ASSERT_MSG(err[0] != '\0', "expected an error message: %s", label);
    rynx_manifest_free(&manifest);
    arena_free(&arena);
}

static void test_manifest_rejects_bad_input(void) {
    expect_invalid("[package]\nversion = \"0.1.0\"\n", "missing name");
    expect_invalid("[package]\nname = \"demo\"\n", "missing version");
    expect_invalid("[package]\nname = \"Demo\"\nversion = \"0.1.0\"\n", "uppercase name");
    expect_invalid("[package]\nname = \"demo\"\nversion = \"0.1.0\"\n"
                   "[dependencies]\nfoo = {}\n", "dependency without a source");
    expect_invalid("[package]\nname = \"demo\"\nversion = \"0.1.0\"\n"
                   "[dependencies]\nfoo = { path = \"../a\", git = \"https://x\" }\n",
                   "dependency with two sources");
    expect_invalid("[package]\nname = \"demo\"\nversion = \"0.1.0\"\n"
                   "[dependencies]\nfoo = \"../a\"\nfoo = \"../b\"\n",
                   "duplicate dependency");
    expect_invalid("[package]\nname = \"demo\"\nversion = \"0.1.0\"\n"
                   "[dependencies]\nfoo = { path = \"../a\", tag = \"v1\" }\n",
                   "tag on a path dependency");
    expect_invalid("[package]\nname = \"demo\"\nversion = unquoted\n", "unquoted value");
    expect_invalid("[unknown]\nname = \"demo\"\n", "unknown section");
    expect_invalid("[package]\nname = \"demo\"\nversion = \"0.1.0\"\n"
                   "[dependencies]\n9foo = \"../a\"\n", "invalid dependency name");
}

static void test_manifest_roundtrip(void) {
    char *dir = ryn_make_temp_dir();
    TEST_ASSERT(dir != NULL);
    if (!dir) {
        return;
    }
    char *path = ryn_path_join(dir, "rynx.toml");

    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[256] = {0};
    TEST_ASSERT(rynx_manifest_parse(VALID_MANIFEST, "test", &arena, &manifest, err, sizeof(err)));
    TEST_ASSERT_MSG(rynx_manifest_write(path, &manifest, err, sizeof(err)),
                    "manifest write failed: %s", err);

    Arena loaded_arena;
    arena_init(&loaded_arena);
    RynxManifest loaded;
    rynx_manifest_init(&loaded);
    TEST_ASSERT_MSG(rynx_manifest_load(path, &loaded_arena, &loaded, err, sizeof(err)),
                    "manifest load failed: %s", err);
    TEST_ASSERT_STR(manifest.name, loaded.name);
    TEST_ASSERT_STR(manifest.version, loaded.version);
    TEST_ASSERT_STR(manifest.entry, loaded.entry);
    TEST_ASSERT_INT(manifest.deps.len, loaded.deps.len);
    for (size_t i = 0; i < loaded.deps.len && i < manifest.deps.len; i++) {
        TEST_ASSERT_STR(manifest.deps.items[i].name, loaded.deps.items[i].name);
        TEST_ASSERT_INT(manifest.deps.items[i].kind, loaded.deps.items[i].kind);
        if (loaded.deps.items[i].kind == RYNX_DEP_PATH) {
            TEST_ASSERT_STR(manifest.deps.items[i].path, loaded.deps.items[i].path);
        } else {
            TEST_ASSERT_STR(manifest.deps.items[i].git, loaded.deps.items[i].git);
            TEST_ASSERT_STR(manifest.deps.items[i].ref, loaded.deps.items[i].ref);
        }
    }
    rynx_manifest_free(&loaded);
    arena_free(&loaded_arena);
    rynx_manifest_free(&manifest);
    arena_free(&arena);
    free(path);
    ryn_remove_temp_dir(dir);
    free(dir);
}

static void test_manifest_editing_and_names(void) {
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[256] = {0};
    TEST_ASSERT(rynx_manifest_parse(VALID_MANIFEST, "test", &arena, &manifest, err, sizeof(err)));

    RynxDep dep;
    memset(&dep, 0, sizeof(dep));
    dep.name = "extra";
    dep.kind = RYNX_DEP_PATH;
    dep.path = "../extra";
    TEST_ASSERT(rynx_manifest_add_dep(&manifest, dep, err, sizeof(err)));
    TEST_ASSERT_INT(4, manifest.deps.len);
    TEST_ASSERT(!rynx_manifest_add_dep(&manifest, dep, err, sizeof(err)));
    TEST_ASSERT(rynx_manifest_remove_dep(&manifest, "greet"));
    TEST_ASSERT_INT(3, manifest.deps.len);
    TEST_ASSERT(rynx_manifest_find_dep(&manifest, "greet") == NULL);
    TEST_ASSERT(!rynx_manifest_remove_dep(&manifest, "greet"));

    TEST_ASSERT(rynx_valid_package_name("hello_world-2"));
    TEST_ASSERT(!rynx_valid_package_name("Hello"));
    TEST_ASSERT(!rynx_valid_package_name("-hello"));
    TEST_ASSERT(!rynx_valid_package_name(""));

    char *name = rynx_sanitize_package_name("My Cool Package!");
    TEST_ASSERT_STR("my-cool-package", name);
    free(name);
    name = rynx_sanitize_package_name("9lives");
    TEST_ASSERT_STR("p9lives", name);
    free(name);
    name = rynx_sanitize_package_name("!!!");
    TEST_ASSERT_STR("package", name);
    free(name);

    rynx_manifest_free(&manifest);
    arena_free(&arena);
}

void run_package_tests(void) {
    test_manifest_parse_valid();
    test_manifest_default_entry();
    test_manifest_rejects_bad_input();
    test_manifest_roundtrip();
    test_manifest_editing_and_names();
}
