/*
 * rynx — the Ryn package manager.
 *
 * Manages a `rynx.toml` manifest, installs path and git dependencies into
 * `.rynx/packages`, records them in `rynx.lock`, and delegates building and
 * running to the `ryn` compiler.
 */
#include "common.h"
#include "package.h"
#include "version.h"

#include <ctype.h>
#include <errno.h>

#if defined(_WIN32)
#  include <direct.h>
#  define getcwd _getcwd
#  define RYNX_EXE_SUFFIX ".exe"
#else
#  include <unistd.h>
#  define RYNX_EXE_SUFFIX ""
#endif

#define RYNX_EXIT_OK 0
#define RYNX_EXIT_ERROR 1
#define RYNX_EXIT_USAGE 2
#define RYNX_EXIT_TOOLCHAIN 3

static void print_version(void) {
    printf("rynx %s (Ryn v%s)\n", RYN_VERSION_STRING, RYN_LANGUAGE_VERSION);
    printf("package manager for the Ryn language\n");
}

static void print_usage(FILE *out) {
    fputs("Rynx " RYN_VERSION_STRING " — the Ryn package manager\n"
          "\n"
          "usage:\n"
          "  rynx <command> [options]\n"
          "\n"
          "commands:\n"
          "  init [name]           create rynx.toml and src/main.ryn\n"
          "  add <path|url>        add a dependency and install it\n"
          "  remove <name>         remove a dependency\n"
          "  install               resolve dependencies and write rynx.lock\n"
          "  list                  list the package and its dependencies\n"
          "  build [args...]       build the entry point with ryn\n"
          "  run [args...]         run the entry point with ryn\n"
          "  clean                 remove .rynx (installed dependencies)\n"
          "  version               print version information\n"
          "  help                  show this help\n"
          "\n"
          "add options:\n"
          "  --name <name>         use a different dependency name\n"
          "  --git                 treat the argument as a git URL\n"
          "  --tag <ref>           git tag to clone\n"
          "  --branch <ref>        git branch to clone\n"
          "  --rev <ref>           git revision to clone\n"
          "\n"
          "global options:\n"
          "  -h, --help            show this help\n"
          "  -V, --version         print version information\n"
          "\n"
          "environment:\n"
          "  RYN                   path to the ryn compiler used by build/run\n",
          out);
}

static int usage_error(const char *fmt, const char *arg) {
    fputs("rynx: error: ", stderr);
    fprintf(stderr, fmt, arg ? arg : "");
    fputs("\n\n", stderr);
    print_usage(stderr);
    return RYNX_EXIT_USAGE;
}

static void errorf(const char *fmt, ...) {
    va_list ap;
    fputs("rynx: error: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static char *current_dir(void) {
    size_t size = 512;
    for (;;) {
        char *buffer = (char *)malloc(size);
        if (!buffer) {
            return NULL;
        }
        if (getcwd(buffer, size)) {
            return buffer;
        }
        free(buffer);
        if (errno != ERANGE) {
            return NULL;
        }
        size *= 2;
    }
}

static bool path_is_absolute(const char *path) {
    if (!path || !*path) {
        return false;
    }
#if defined(_WIN32)
    if (isalpha((unsigned char)path[0]) && path[1] == ':') {
        return true;
    }
    return path[0] == '\\' || path[0] == '/';
#else
    return path[0] == '/';
#endif
}

static char *join_root(const char *root, const char *relative) {
    if (path_is_absolute(relative)) {
        return ryn_strdup(relative);
    }
    return ryn_path_join(root, relative);
}

static bool looks_like_git(const char *spec) {
    return strncmp(spec, "http://", 7) == 0 || strncmp(spec, "https://", 8) == 0 ||
           strncmp(spec, "ssh://", 6) == 0 || strncmp(spec, "git://", 6) == 0 ||
           strncmp(spec, "git@", 4) == 0;
}

static bool ensure_directory(const char *path, char *err, size_t errlen) {
    if (ryn_is_directory(path)) {
        return true;
    }
    if (ryn_file_exists(path)) {
        snprintf(err, errlen, "'%s' exists but is not a directory", path);
        return false;
    }
    if (!ryn_make_dir(path)) {
        snprintf(err, errlen, "cannot create directory '%s'", path);
        return false;
    }
    return true;
}

static bool ensure_state_dirs(const char *root, char *err, size_t errlen) {
    char *state = ryn_path_join(root, RYNX_STATE_DIR);
    char *packages = ryn_path_join(root, RYNX_PACKAGES_DIR);
    bool ok = ensure_directory(state, err, errlen) && ensure_directory(packages, err, errlen);
    free(state);
    free(packages);
    return ok;
}

static bool load_project(const char *root, Arena *arena, RynxManifest *manifest, char *err,
                         size_t errlen) {
    char *path = ryn_path_join(root, RYNX_MANIFEST_FILE);
    bool ok;
    if (!ryn_file_exists(path)) {
        snprintf(err, errlen, "no %s found in '%s' (run `rynx init`)", RYNX_MANIFEST_FILE, root);
        ok = false;
    } else {
        ok = rynx_manifest_load(path, arena, manifest, err, errlen);
    }
    free(path);
    return ok;
}

static bool write_file_text(const char *path, const char *text, char *err, size_t errlen) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        snprintf(err, errlen, "cannot write '%s': %s", path, strerror(errno));
        return false;
    }
    bool ok = fputs(text, file) >= 0;
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        snprintf(err, errlen, "failed to write '%s'", path);
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* init                                                               */
/* ------------------------------------------------------------------ */

static int cmd_init(int argc, char **argv) {
    if (argc > 1) {
        return usage_error("'init' takes at most one package name", NULL);
    }
    char *root = current_dir();
    if (!root) {
        errorf("cannot determine the current directory");
        return RYNX_EXIT_ERROR;
    }
    char *manifest_path = ryn_path_join(root, RYNX_MANIFEST_FILE);
    if (ryn_file_exists(manifest_path)) {
        errorf("'%s' already exists", manifest_path);
        free(manifest_path);
        free(root);
        return RYNX_EXIT_ERROR;
    }

    const char *requested = argc == 1 ? argv[0] : ryn_path_basename(root);
    char *name = rynx_sanitize_package_name(requested);
    if (argc != 1) {
        free((char *)requested);
    }

    char *src_dir = ryn_path_join(root, "src");
    char *main_path = ryn_path_join(src_dir, "main.ryn");
    char err[512];
    if (!ensure_directory(src_dir, err, sizeof(err))) {
        errorf("%s", err);
        free(src_dir);
        free(main_path);
        free(name);
        free(manifest_path);
        free(root);
        return RYNX_EXIT_ERROR;
    }

    bool created_main = false;
    if (!ryn_file_exists(main_path)) {
        StrBuf source;
        sb_init(&source);
        sb_appendf(&source,
                   "fn main() -> i32:\n"
                   "    println(\"Hello from %s!\")\n"
                   "    return 0\n",
                   name);
        bool ok = write_file_text(main_path, source.data ? source.data : "", err, sizeof(err));
        sb_free(&source);
        if (!ok) {
            errorf("%s", err);
            free(src_dir);
            free(main_path);
            free(name);
            free(manifest_path);
            free(root);
            return RYNX_EXIT_ERROR;
        }
        created_main = true;
    }

    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    manifest.name = arena_strdup(&arena, name);
    manifest.version = arena_strdup(&arena, "0.1.0");
    manifest.entry = arena_strdup(&arena, RYNX_DEFAULT_ENTRY);

    if (!rynx_manifest_write(manifest_path, &manifest, err, sizeof(err))) {
        errorf("%s", err);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(src_dir);
        free(main_path);
        free(name);
        free(manifest_path);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    printf("created %s\n", RYNX_MANIFEST_FILE);
    if (created_main) {
        printf("created src/main.ryn\n");
    } else {
        printf("kept existing src/main.ryn\n");
    }
    printf("next: rynx run\n");

    rynx_manifest_free(&manifest);
    arena_free(&arena);
    free(src_dir);
    free(main_path);
    free(name);
    free(manifest_path);
    free(root);
    return RYNX_EXIT_OK;
}

/* ------------------------------------------------------------------ */
/* install                                                            */
/* ------------------------------------------------------------------ */

static bool dependency_manifest(const char *dir, Arena *arena, RynxManifest *dep_manifest,
                                char *err, size_t errlen) {
    char *path = ryn_path_join(dir, RYNX_MANIFEST_FILE);
    bool ok;
    if (!ryn_file_exists(path)) {
        snprintf(err, errlen, "'%s' has no %s", dir, RYNX_MANIFEST_FILE);
        ok = false;
    } else {
        ok = rynx_manifest_load(path, arena, dep_manifest, err, errlen);
    }
    free(path);
    return ok;
}

static bool install_git_dependency(const char *root, RynxDep *dep, Arena *arena, char *err,
                                   size_t errlen) {
    char *packages = ryn_path_join(root, RYNX_PACKAGES_DIR);
    char *target = ryn_path_join(packages, dep->name);
    bool installed = ryn_is_directory(target);

    if (!installed) {
        char **args = (char **)calloc(10, sizeof(char *));
        if (!args) {
            free(packages);
            free(target);
            snprintf(err, errlen, "out of memory");
            return false;
        }
        size_t count = 0;
        args[count++] = (char *)"git";
        args[count++] = (char *)"clone";
        args[count++] = (char *)"--depth";
        args[count++] = (char *)"1";
        if (dep->ref) {
            args[count++] = (char *)"--branch";
            args[count++] = (char *)dep->ref;
        }
        args[count++] = (char *)"--";
        args[count++] = (char *)dep->git;
        args[count++] = target;
        int code = ryn_spawn_process(args);
        free(args);
        if (code != 0) {
            snprintf(err, errlen, "git clone of '%s' failed (exit %d)", dep->git, code);
            free(packages);
            free(target);
            return false;
        }
    }

    RynxManifest dep_manifest;
    rynx_manifest_init(&dep_manifest);
    bool ok = dependency_manifest(target, arena, &dep_manifest, err, errlen);
    if (ok && dep_manifest.version) {
        dep->version = dep_manifest.version;
    }
    rynx_manifest_free(&dep_manifest);
    free(packages);
    free(target);
    return ok;
}

static bool install_path_dependency(const char *root, RynxDep *dep, Arena *arena, char *err,
                                    size_t errlen) {
    char *resolved = join_root(root, dep->path);
    bool ok = true;
    if (!ryn_is_directory(resolved)) {
        snprintf(err, errlen, "path dependency '%s' does not exist at '%s'", dep->name, resolved);
        ok = false;
    } else {
        RynxManifest dep_manifest;
        rynx_manifest_init(&dep_manifest);
        ok = dependency_manifest(resolved, arena, &dep_manifest, err, errlen);
        if (ok && dep_manifest.version) {
            dep->version = dep_manifest.version;
        }
        rynx_manifest_free(&dep_manifest);
    }
    free(resolved);
    return ok;
}

static int install_project(const char *root, RynxManifest *manifest) {
    char err[512];
    if (!ensure_state_dirs(root, err, sizeof(err))) {
        errorf("%s", err);
        return RYNX_EXIT_ERROR;
    }
    Arena arena;
    arena_init(&arena);
    for (size_t i = 0; i < manifest->deps.len; i++) {
        RynxDep *dep = &manifest->deps.items[i];
        bool ok = dep->kind == RYNX_DEP_PATH
                      ? install_path_dependency(root, dep, &arena, err, sizeof(err))
                      : install_git_dependency(root, dep, &arena, err, sizeof(err));
        if (!ok) {
            errorf("%s", err);
            arena_free(&arena);
            return RYNX_EXIT_ERROR;
        }
    }

    char *lock_path = ryn_path_join(root, RYNX_LOCK_FILE);
    bool ok = rynx_lock_write(lock_path, manifest, err, sizeof(err));
    free(lock_path);
    arena_free(&arena);
    if (!ok) {
        errorf("%s", err);
        return RYNX_EXIT_ERROR;
    }
    printf("installed %zu dependenc%s\n", manifest->deps.len,
           manifest->deps.len == 1 ? "y" : "ies");
    return RYNX_EXIT_OK;
}

static int cmd_install(void) {
    char *root = current_dir();
    if (!root) {
        errorf("cannot determine the current directory");
        return RYNX_EXIT_ERROR;
    }
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[512];
    if (!load_project(root, &arena, &manifest, err, sizeof(err))) {
        errorf("%s", err);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    int code = install_project(root, &manifest);
    rynx_manifest_free(&manifest);
    arena_free(&arena);
    free(root);
    return code;
}

/* ------------------------------------------------------------------ */
/* add / remove                                                       */
/* ------------------------------------------------------------------ */

static int cmd_add(int argc, char **argv) {
    const char *spec = NULL;
    const char *name_option = NULL;
    const char *ref = NULL;
    bool force_git = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0) {
            if (i + 1 >= argc) {
                return usage_error("option '%s' requires a value", argv[i]);
            }
            name_option = argv[++i];
        } else if (strcmp(argv[i], "--git") == 0) {
            force_git = true;
        } else if (strcmp(argv[i], "--tag") == 0 || strcmp(argv[i], "--branch") == 0 ||
                   strcmp(argv[i], "--rev") == 0) {
            if (i + 1 >= argc) {
                return usage_error("option '%s' requires a value", argv[i]);
            }
            ref = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            return usage_error("unknown option '%s'", argv[i]);
        } else if (!spec) {
            spec = argv[i];
        } else {
            return usage_error("unexpected argument '%s'", argv[i]);
        }
    }
    if (!spec) {
        return usage_error("'add' requires a path or git URL", NULL);
    }
    if (ref && !force_git && !looks_like_git(spec)) {
        errorf("'--tag', '--branch' and '--rev' are only valid for git dependencies");
        return RYNX_EXIT_USAGE;
    }
    if ((force_git || looks_like_git(spec)) && spec[0] == '-') {
        errorf("invalid git URL '%s'", spec);
        return RYNX_EXIT_USAGE;
    }

    char *root = current_dir();
    if (!root) {
        errorf("cannot determine the current directory");
        return RYNX_EXIT_ERROR;
    }
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[512];
    if (!load_project(root, &arena, &manifest, err, sizeof(err))) {
        errorf("%s", err);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }

    bool is_git = force_git || looks_like_git(spec);
    char *base = ryn_path_basename(spec);
    size_t base_len = strlen(base);
    if (base_len > 4 && strcmp(base + base_len - 4, ".git") == 0) {
        base[base_len - 4] = '\0';
    }
    char *derived = rynx_sanitize_package_name(base);
    free(base);

    RynxDep dep;
    memset(&dep, 0, sizeof(dep));
    dep.name = name_option ? arena_strdup(&arena, name_option)
                           : arena_strdup(&arena, derived);
    free(derived);
    if (!rynx_valid_package_name(dep.name)) {
        errorf("invalid package name '%s' (use lowercase letters, digits, '_' or '-')",
               dep.name);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }

    if (is_git) {
        dep.kind = RYNX_DEP_GIT;
        dep.git = arena_strdup(&arena, spec);
        dep.ref = ref ? arena_strdup(&arena, ref) : NULL;
    } else {
        char *resolved = join_root(root, spec);
        RynxManifest dep_manifest;
        rynx_manifest_init(&dep_manifest);
        if (!dependency_manifest(resolved, &arena, &dep_manifest, err, sizeof(err))) {
            errorf("%s", err);
            rynx_manifest_free(&dep_manifest);
            free(resolved);
            rynx_manifest_free(&manifest);
            arena_free(&arena);
            free(root);
            return RYNX_EXIT_ERROR;
        }
        if (!name_option) {
            dep.name = dep_manifest.name;
        }
        dep.kind = RYNX_DEP_PATH;
        dep.path = arena_strdup(&arena, spec);
        dep.version = dep_manifest.version;
        rynx_manifest_free(&dep_manifest);
        free(resolved);
    }

    if (!rynx_manifest_add_dep(&manifest, dep, err, sizeof(err))) {
        errorf("%s", err);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }

    char *manifest_path = ryn_path_join(root, RYNX_MANIFEST_FILE);
    bool written = rynx_manifest_write(manifest_path, &manifest, err, sizeof(err));
    free(manifest_path);
    if (!written) {
        errorf("%s", err);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    printf("added %s\n", dep.name);
    int code = install_project(root, &manifest);
    rynx_manifest_free(&manifest);
    arena_free(&arena);
    free(root);
    return code;
}

static int cmd_remove(int argc, char **argv) {
    if (argc != 1) {
        return usage_error("usage: rynx remove <name>", NULL);
    }
    if (argv[0][0] == '-' && argv[0][1] != '\0') {
        return usage_error("unknown option '%s'", argv[0]);
    }
    char *root = current_dir();
    if (!root) {
        errorf("cannot determine the current directory");
        return RYNX_EXIT_ERROR;
    }
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[512];
    if (!load_project(root, &arena, &manifest, err, sizeof(err))) {
        errorf("%s", err);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    if (!rynx_manifest_remove_dep(&manifest, argv[0])) {
        errorf("dependency '%s' is not declared", argv[0]);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    char *manifest_path = ryn_path_join(root, RYNX_MANIFEST_FILE);
    bool written = rynx_manifest_write(manifest_path, &manifest, err, sizeof(err));
    free(manifest_path);
    if (!written) {
        errorf("%s", err);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    char *installed = ryn_path_join(root, RYNX_PACKAGES_DIR);
    char *target = ryn_path_join(installed, argv[0]);
    ryn_remove_tree(target);
    free(target);
    free(installed);
    printf("removed %s\n", argv[0]);
    int code = install_project(root, &manifest);
    rynx_manifest_free(&manifest);
    arena_free(&arena);
    free(root);
    return code;
}

/* ------------------------------------------------------------------ */
/* list / clean                                                       */
/* ------------------------------------------------------------------ */

static int cmd_list(void) {
    char *root = current_dir();
    if (!root) {
        errorf("cannot determine the current directory");
        return RYNX_EXIT_ERROR;
    }
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[512];
    if (!load_project(root, &arena, &manifest, err, sizeof(err))) {
        errorf("%s", err);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    printf("%s %s (%s)\n", manifest.name, manifest.version, manifest.entry);
    if (manifest.deps.len == 0) {
        printf("dependencies: none\n");
    } else {
        printf("dependencies:\n");
        for (size_t i = 0; i < manifest.deps.len; i++) {
            const RynxDep *dep = &manifest.deps.items[i];
            printf("  %s", dep->name);
            if (dep->version) {
                printf(" %s", dep->version);
            }
            if (dep->kind == RYNX_DEP_PATH) {
                printf(" (path %s)\n", dep->path);
            } else {
                printf(" (git %s", dep->git);
                if (dep->ref) {
                    printf(" @ %s", dep->ref);
                }
                printf(")\n");
            }
        }
    }
    rynx_manifest_free(&manifest);
    arena_free(&arena);
    free(root);
    return RYNX_EXIT_OK;
}

static int cmd_clean(void) {
    char *root = current_dir();
    if (!root) {
        errorf("cannot determine the current directory");
        return RYNX_EXIT_ERROR;
    }
    char *state = ryn_path_join(root, RYNX_STATE_DIR);
    if (!ryn_is_directory(state)) {
        printf("nothing to clean\n");
        free(state);
        free(root);
        return RYNX_EXIT_OK;
    }
    ryn_remove_tree(state);
    printf("removed %s\n", RYNX_STATE_DIR);
    free(state);
    free(root);
    return RYNX_EXIT_OK;
}

/* ------------------------------------------------------------------ */
/* build / run                                                        */
/* ------------------------------------------------------------------ */

static char *find_ryn(const char *self) {
    char *from_env = ryn_getenv("RYN");
    if (from_env) {
        return from_env;
    }
    char *dir = ryn_path_dirname(self);
    StrBuf candidate;
    sb_init(&candidate);
    sb_appendf(&candidate, "%s/ryn%s", dir, RYNX_EXE_SUFFIX);
    free(dir);
    if (ryn_file_exists(candidate.data)) {
        char *result = sb_detach(&candidate);
        return result;
    }
    sb_free(&candidate);
    return ryn_strdup("ryn");
}

static int run_ryn(const char *self, const char *command, int argc, char **argv) {
    char *root = current_dir();
    if (!root) {
        errorf("cannot determine the current directory");
        return RYNX_EXIT_ERROR;
    }
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[512];
    if (!load_project(root, &arena, &manifest, err, sizeof(err))) {
        errorf("%s", err);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    char *entry = join_root(root, manifest.entry);
    if (!ryn_file_exists(entry)) {
        errorf("entry point '%s' does not exist", entry);
        free(entry);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }

    char *ryn = find_ryn(self);
    char **child = (char **)calloc((size_t)argc + 4, sizeof(char *));
    if (!child) {
        errorf("out of memory");
        free(ryn);
        free(entry);
        rynx_manifest_free(&manifest);
        arena_free(&arena);
        free(root);
        return RYNX_EXIT_ERROR;
    }
    size_t count = 0;
    child[count++] = ryn;
    child[count++] = (char *)command;
    child[count++] = entry;
    for (int i = 0; i < argc; i++) {
        child[count++] = argv[i];
    }
    child[count] = NULL;

    int code = ryn_spawn_process(child);
    if (code < 0) {
        errorf("could not run '%s'; set RYN to the compiler path", ryn);
        code = RYNX_EXIT_TOOLCHAIN;
    }
    free(child);
    free(ryn);
    free(entry);
    rynx_manifest_free(&manifest);
    arena_free(&arena);
    free(root);
    return code;
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(stderr);
        return RYNX_EXIT_USAGE;
    }
    const char *command = argv[1];
    if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0 ||
        strcmp(command, "-V") == 0) {
        print_version();
        return RYNX_EXIT_OK;
    }
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 ||
        strcmp(command, "-h") == 0) {
        print_usage(stdout);
        return RYNX_EXIT_OK;
    }
    if (strcmp(command, "init") == 0) {
        return cmd_init(argc - 2, argv + 2);
    }
    if (strcmp(command, "add") == 0) {
        return cmd_add(argc - 2, argv + 2);
    }
    if (strcmp(command, "remove") == 0) {
        return cmd_remove(argc - 2, argv + 2);
    }
    if (strcmp(command, "install") == 0) {
        if (argc > 2) {
            return usage_error("'install' takes no arguments", NULL);
        }
        return cmd_install();
    }
    if (strcmp(command, "list") == 0) {
        if (argc > 2) {
            return usage_error("'list' takes no arguments", NULL);
        }
        return cmd_list();
    }
    if (strcmp(command, "clean") == 0) {
        if (argc > 2) {
            return usage_error("'clean' takes no arguments", NULL);
        }
        return cmd_clean();
    }
    if (strcmp(command, "build") == 0) {
        return run_ryn(argv[0], "build", argc - 2, argv + 2);
    }
    if (strcmp(command, "run") == 0) {
        return run_ryn(argv[0], "run", argc - 2, argv + 2);
    }
    return usage_error("unknown command '%s'", command);
}
