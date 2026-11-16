#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "compiler.h"

#include "ast.h"
#include "builtins.h"
#include "codegen.h"
#include "diagnostics.h"
#include "ir.h"
#include "module.h"
#include "sema.h"
#include "source.h"
#include "types.h"
#include "version.h"

#if defined(_WIN32)
#  include <io.h>
#  include <process.h>
#  include <windows.h>
#else
#  include <errno.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static SourcePos no_position(void) {
    SourcePos pos;
    pos.file = -1;
    pos.line = 1;
    pos.column = 1;
    pos.offset = 0;
    return pos;
}

static char *default_output_path(const CompilerOptions *options, bool executable) {
    char *base = ryn_path_without_ext(options->input_path);
    if (!executable) {
        StrBuf sb;
        sb_init(&sb);
        sb_appendf(&sb, "%s.c", base);
        free(base);
        char *result = sb_detach(&sb);
        return result;
    }
#if defined(_WIN32)
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "%s.exe", base);
    free(base);
    return sb_detach(&sb);
#else
    return base;
#endif
}

/* ------------------------------------------------------------------ */
/* Runtime and toolchain discovery                                    */
/* ------------------------------------------------------------------ */

static bool directory_has_runtime(const char *dir) {
    char *header = ryn_path_join(dir, "ryn_runtime.h");
    char *source = ryn_path_join(dir, "runtime.c");
    bool ok = ryn_file_exists(header) && ryn_file_exists(source);
    free(header);
    free(source);
    return ok;
}

/* Best effort absolute path of the running compiler, used to find the runtime
 * next to the binary even when the program was started through PATH. */
static bool current_executable_path(char *buffer, size_t length, const char *fallback) {
#if defined(_WIN32)
    DWORD written = GetModuleFileNameA(NULL, buffer, (DWORD)length);
    if (written > 0 && (size_t)written < length) {
        return true;
    }
#elif defined(__linux__)
    ssize_t written = readlink("/proc/self/exe", buffer, length - 1);
    if (written > 0 && (size_t)written < length) {
        buffer[written] = '\0';
        return true;
    }
#endif
    if (fallback && *fallback) {
        snprintf(buffer, length, "%s", fallback);
        return true;
    }
    return false;
}

static char *find_runtime_dir(const CompilerOptions *options) {
    char *from_env = ryn_getenv("RYN_RUNTIME_DIR");
    if (from_env && *from_env) {
        if (directory_has_runtime(from_env)) {
            return from_env;
        }
        free(from_env);
    } else {
        free(from_env);
    }

    char executable[4096];
    const char *program = options->program_path;
    if (current_executable_path(executable, sizeof(executable), options->program_path)) {
        program = executable;
    }
    if (program && *program) {
        char *exe_dir = ryn_path_dirname(program);
        if (exe_dir) {
            char *candidates[3];
            size_t count = 0;
            candidates[count++] = ryn_path_join(exe_dir, "runtime");
            candidates[count++] = ryn_path_join(exe_dir, "../runtime");
            candidates[count++] = ryn_path_join(exe_dir, "../share/ryn/runtime");
            for (size_t i = 0; i < count; i++) {
                if (candidates[i] && directory_has_runtime(candidates[i])) {
                    char *chosen = ryn_strdup(candidates[i]);
                    for (size_t j = 0; j < count; j++) {
                        free(candidates[j]);
                    }
                    free(exe_dir);
                    return chosen;
                }
            }
            for (size_t i = 0; i < count; i++) {
                free(candidates[i]);
            }
            free(exe_dir);
        }
    }

    if (directory_has_runtime(RYN_RUNTIME_DIR)) {
        return ryn_strdup(RYN_RUNTIME_DIR);
    }
    return NULL;
}

static const char *default_cc(void) {
    const char *env = getenv("CC");
    if (env && *env) {
        return env;
    }
#if defined(_WIN32)
    return "gcc";
#else
    return "cc";
#endif
}

/* ------------------------------------------------------------------ */
/* Process spawning                                                   */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Emitting the generated C file                                      */
/* ------------------------------------------------------------------ */

static bool emit_c_file(const CompilerOptions *options, Arena *arena, Diagnostics *diag,
                        AstProgram *program, IrModule *module, TypeContext *types,
                        const char *path) {
    FILE *out = NULL;
    bool to_stdout = strcmp(path, "-") == 0;
    if (to_stdout) {
        out = stdout;
    } else {
        out = fopen(path, "wb");
        if (!out) {
            diag_error(diag, no_position(), E_IO, "cannot write '%s': %s", path, strerror(errno));
            return false;
        }
    }

    CodegenOptions cg;
    memset(&cg, 0, sizeof(cg));
    cg.source_path = options->input_path;
    cg.debug = options->debug && !options->release;
    cg.includes = options->includes;
    cg.nincludes = options->nincludes;

    bool ok = codegen_emit(out, arena, diag, program, module, types, &cg);
    if (!to_stdout) {
        if (fclose(out) != 0 && ok) {
            diag_error(diag, no_position(), E_IO, "cannot finish writing '%s'", path);
            ok = false;
        }
    } else {
        fflush(stdout);
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Building a native executable                                       */
/* ------------------------------------------------------------------ */

#define RYN_RUNTIME_SOURCE_COUNT 5
static const char *runtime_sources[RYN_RUNTIME_SOURCE_COUNT] = {
    "runtime.c", "io.c", "math.c", "str.c", "mem.c",
};

static char **build_cc_argv(const CompilerOptions *options, const char *cc, const char *c_path,
                            const char *exe_path, const char *runtime_dir,
                            char *runtime_paths[RYN_RUNTIME_SOURCE_COUNT], size_t *out_count) {
    size_t capacity = 16 + RYN_RUNTIME_SOURCE_COUNT;
    char **argv = (char **)calloc(capacity, sizeof(char *));
    if (!argv) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    size_t n = 0;
    argv[n++] = (char *)cc;
    argv[n++] = (char *)"-std=c11";
    if (options->release) {
        argv[n++] = (char *)"-O2";
    } else {
        argv[n++] = (char *)"-O0";
        argv[n++] = (char *)"-g";
    }
    argv[n++] = (char *)"-I";
    argv[n++] = (char *)runtime_dir;
    argv[n++] = (char *)"-o";
    argv[n++] = (char *)exe_path;
    argv[n++] = (char *)c_path;
    for (size_t i = 0; i < RYN_RUNTIME_SOURCE_COUNT; i++) {
        runtime_paths[i] = ryn_path_join(runtime_dir, runtime_sources[i]);
        argv[n++] = runtime_paths[i];
    }
#if !defined(_WIN32)
    argv[n++] = (char *)"-lm";
#endif
    argv[n] = NULL;
    *out_count = n;
    return argv;
}

static void free_cc_argv(char **argv, char *runtime_paths[RYN_RUNTIME_SOURCE_COUNT]) {
    for (size_t i = 0; i < RYN_RUNTIME_SOURCE_COUNT; i++) {
        free(runtime_paths[i]);
    }
    free(argv);
}

static int compile_to_executable(const CompilerOptions *options, Arena *arena, Diagnostics *diag,
                                 AstProgram *program, IrModule *module, TypeContext *types,
                                 const char *runtime_dir, const char *c_path,
                                 const char *exe_path) {
    if (!emit_c_file(options, arena, diag, program, module, types, c_path)) {
        return RYN_EXIT_COMPILE_ERROR;
    }
    if (diag->error_count > 0) {
        return RYN_EXIT_COMPILE_ERROR;
    }

    const char *cc = options->cc && *options->cc ? options->cc : default_cc();
    size_t argc = 0;
    char *runtime_paths[RYN_RUNTIME_SOURCE_COUNT] = {0};
    char **argv = build_cc_argv(options, cc, c_path, exe_path, runtime_dir, runtime_paths, &argc);
    int code = ryn_spawn_process(argv);
    free_cc_argv(argv, runtime_paths);
    if (code < 0) {
        diag_error(diag, no_position(), E_IO, "failed to run the C compiler '%s'", cc);
        return RYN_EXIT_TOOLCHAIN;
    }
    if (code != 0) {
        diag_error_note(diag, no_position(), 1, E_IO,
                        "the generated C code or the toolchain reported a problem",
                        "C compiler '%s' exited with code %d", cc, code);
        return RYN_EXIT_TOOLCHAIN;
    }
    RYN_UNUSED(argc);
    return RYN_EXIT_OK;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

int compiler_execute(const CompilerOptions *options) {
    Arena arena;
    arena_init(&arena);
    SourceManager sm;
    sm_init(&sm, &arena);
    Diagnostics diag;
    diag_init(&diag);
#if defined(_WIN32)
    diag_set_color(&diag, _isatty(_fileno(stderr)) != 0);
#else
    diag_set_color(&diag, isatty(fileno(stderr)) != 0);
#endif

    int result = RYN_EXIT_OK;
    Sema sema;
    bool sema_ready = false;
    IrModule *module = NULL;
    char *temp_dir = NULL;
    char *c_path = NULL;
    char *exe_path = NULL;
    char *runtime_dir = NULL;
    AstProgram *program = NULL;
    TypeContext types;
    memset(&types, 0, sizeof(types));

    if (!options->input_path) {
        diag_error(&diag, no_position(), E_NO_INPUT, "no input file given");
        result = RYN_EXIT_USAGE;
        goto finish;
    }
    if (options->target && strcmp(options->target, "native") != 0) {
        diag_error(&diag, no_position(), E_UNSUPPORTED,
                   "unknown target '%s' (only 'native' is available in Ryn %s)",
                   options->target, RYN_VERSION_STRING);
        result = RYN_EXIT_USAGE;
        goto finish;
    }

    ModuleOptions module_options;
    module_options.search_paths = options->module_paths;
    module_options.nsearch_paths = options->nmodule_paths;
    if (!modules_load_program(&arena, &diag, &sm, options->input_path, &module_options,
                              options->dump_tokens, &program)) {
        result = RYN_EXIT_COMPILE_ERROR;
        goto finish;
    }
    if (options->dump_ast && program) {
        ast_dump_program(program, stdout);
    }
    if (!program || diag.error_count > 0) {
        result = RYN_EXIT_COMPILE_ERROR;
        goto finish;
    }

    types_init(&types, &arena);
    sema_init(&sema, &arena, &diag, &sm);
    sema_ready = true;
    bool analyzed = sema_analyze(&sema, program);
    if (!analyzed || diag.error_count > 0) {
        result = RYN_EXIT_COMPILE_ERROR;
        goto finish;
    }

    module = ir_build(&arena, &diag, program, &types);
    if (!module || diag.error_count > 0) {
        result = RYN_EXIT_COMPILE_ERROR;
        goto finish;
    }
    if (options->dump_ir) {
        ir_dump_module(module, stdout);
    }

    if (options->command == COMPILE_CHECK) {
        result = RYN_EXIT_OK;
        goto finish;
    }

    bool needs_toolchain = options->command == COMPILE_BUILD || options->command == COMPILE_RUN;

    if (options->command == COMPILE_TRANSPILE) {
        char *default_path = NULL;
        const char *out_path = options->output_path;
        if (!out_path) {
            default_path = default_output_path(options, false);
            out_path = default_path;
        }
        bool ok = emit_c_file(options, &arena, &diag, program, module, &types, out_path);
        if (!ok || diag.error_count > 0) {
            result = RYN_EXIT_COMPILE_ERROR;
        } else if (strcmp(out_path, "-") != 0) {
            printf("%s\n", out_path);
        }
        free(default_path);
        goto finish;
    }

    if (needs_toolchain) {
        runtime_dir = find_runtime_dir(options);
        if (!runtime_dir) {
            diag_error_note(&diag, no_position(), 1, E_IO,
                            "set RYN_RUNTIME_DIR to the directory that contains ryn_runtime.h",
                            "cannot find the Ryn runtime library used by generated code");
            result = RYN_EXIT_TOOLCHAIN;
            goto finish;
        }
        temp_dir = ryn_make_temp_dir();
        if (!temp_dir) {
            diag_error(&diag, no_position(), E_IO, "cannot create a temporary directory");
            result = RYN_EXIT_TOOLCHAIN;
            goto finish;
        }
        c_path = ryn_path_join(temp_dir, "program.c");
        if (options->command == COMPILE_RUN) {
#if defined(_WIN32)
            exe_path = ryn_path_join(temp_dir, "program.exe");
#else
            exe_path = ryn_path_join(temp_dir, "program");
#endif
        } else {
            exe_path = options->output_path ? ryn_strdup(options->output_path)
                                            : default_output_path(options, true);
        }

        int code = compile_to_executable(options, &arena, &diag, program, module, &types,
                                         runtime_dir, c_path, exe_path);
        if (code != RYN_EXIT_OK) {
            result = code;
            goto finish;
        }

        if (options->command == COMPILE_RUN) {
            size_t argc = (size_t)options->nprogram_args + 2;
            char **argv = (char **)calloc(argc, sizeof(char *));
            if (!argv) {
                fprintf(stderr, "ryn: out of memory\n");
                abort();
            }
            argv[0] = exe_path;
            for (int i = 0; i < options->nprogram_args; i++) {
                argv[(size_t)i + 1] = options->program_args[i];
            }
            argv[argc - 1] = NULL;
            int run_code = ryn_spawn_process(argv);
            free(argv);
            if (run_code < 0) {
                diag_error(&diag, no_position(), E_IO, "failed to start the compiled program");
                result = RYN_EXIT_TOOLCHAIN;
            } else {
                result = run_code;
            }
        } else {
            printf("%s\n", exe_path);
        }
    }

finish:
    if (diag.items.len > 0) {
        diag_render_all(&diag, &sm, stderr);
    }
    if (temp_dir) {
        ryn_remove_temp_dir(temp_dir);
    }
    free(c_path);
    free(exe_path);
    free(runtime_dir);
    free(temp_dir);
    if (module) {
        ir_module_free(module);
    }
    if (program) {
        ast_program_free(program);
    }
    if (sema_ready) {
        sema_free(&sema);
    }
    diag_free(&diag);
    sm_free(&sm);
    arena_free(&arena);
    return result;
}
