/*
 * Ryn programming language — command line driver.
 *
 * Parses the command line, then hands control to compiler.c. All output that
 * is not a compiler diagnostic goes to stdout; diagnostics go to stderr.
 */
#include "common.h"
#include "compiler.h"
#include "version.h"

typedef struct CliState {
    const char *command;
    CompilerOptions options;
    const char **includes;
    size_t nincludes;
    size_t include_capacity;
    char **program_args;
    size_t nprogram_args;
    size_t program_arg_capacity;
} CliState;

static void print_version(void) {
    printf("ryn %s (%s)\n", RYN_VERSION_STRING, RYN_CODENAME);
    printf("language version %s\n", RYN_LANGUAGE_VERSION);
    printf("C17 compiler, C11 backend\n");
}

static void print_usage(FILE *out) {
    fputs("Ryn " RYN_VERSION_STRING " (" RYN_CODENAME ") — the Ryn compiler\n"
          "\n"
          "usage:\n"
          "  ryn <command> [options] <file.ryn> [-- program args]\n"
          "\n"
          "commands:\n"
          "  check        type-check a program without generating code\n"
          "  transpile    translate a program to C11\n"
          "  build        compile a program to a native executable\n"
          "  run          build and run a program\n"
          "  version      print version information\n"
          "  help         show this help\n"
          "\n"
          "options:\n"
          "  -o <path>      output file for transpile or build\n"
          "  --debug        build with -O0 -g (default)\n"
          "  --release      build with -O2\n"
          "  --target <t>   target backend; only 'native' is available\n"
          "  --cc <path>    C compiler to use (default: $CC or cc)\n"
          "  --include <h>  add '#include <h>' to the generated C (repeatable)\n"
          "  --dump-tokens  print the token stream\n"
          "  --dump-ast     print the syntax tree\n"
          "  --emit-ir      print the linear intermediate representation\n"
          "  -h, --help     show this help\n"
          "  -V, --version  print version information\n"
          "\n"
          "exit codes:\n"
          "  0 success, 1 compilation error, 2 usage error, 3 toolchain or I/O failure\n",
          out);
}

static bool push_include(CliState *cli, const char *value) {
    if (cli->nincludes == cli->include_capacity) {
        size_t cap = cli->include_capacity ? cli->include_capacity * 2 : 8;
        const char **grown = (const char **)realloc(cli->includes, cap * sizeof(char *));
        if (!grown) {
            return false;
        }
        cli->includes = grown;
        cli->include_capacity = cap;
    }
    cli->includes[cli->nincludes++] = value;
    return true;
}

static bool push_program_arg(CliState *cli, char *value) {
    if (cli->nprogram_args == cli->program_arg_capacity) {
        size_t cap = cli->program_arg_capacity ? cli->program_arg_capacity * 2 : 8;
        char **grown = (char **)realloc(cli->program_args, cap * sizeof(char *));
        if (!grown) {
            return false;
        }
        cli->program_args = grown;
        cli->program_arg_capacity = cap;
    }
    cli->program_args[cli->nprogram_args++] = value;
    return true;
}

static int usage_error(const char *fmt, const char *arg) {
    fputs("ryn: error: ", stderr);
    fprintf(stderr, fmt, arg ? arg : "");
    fputs("\n\n", stderr);
    print_usage(stderr);
    return RYN_EXIT_USAGE;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(stderr);
        return RYN_EXIT_USAGE;
    }

    const char *command = argv[1];
    if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0 ||
        strcmp(command, "-V") == 0) {
        print_version();
        return RYN_EXIT_OK;
    }
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 ||
        strcmp(command, "-h") == 0) {
        print_usage(stdout);
        return RYN_EXIT_OK;
    }

    CliState cli;
    memset(&cli, 0, sizeof(cli));
    cli.command = command;
    cli.options = (CompilerOptions){0};
    cli.options.command = COMPILE_CHECK;
    cli.options.debug = true;

    if (strcmp(command, "check") == 0) {
        cli.options.command = COMPILE_CHECK;
    } else if (strcmp(command, "transpile") == 0) {
        cli.options.command = COMPILE_TRANSPILE;
    } else if (strcmp(command, "build") == 0) {
        cli.options.command = COMPILE_BUILD;
    } else if (strcmp(command, "run") == 0) {
        cli.options.command = COMPILE_RUN;
    } else {
        return usage_error("unknown command '%s'", command);
    }

    const char *input = NULL;
    bool after_separator = false;

    for (int i = 2; i < argc; i++) {
        char *arg = argv[i];
        if (after_separator) {
            if (!push_program_arg(&cli, arg)) {
                return RYN_EXIT_USAGE;
            }
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            if (cli.options.command != COMPILE_RUN) {
                return usage_error("program arguments ('--') are only valid with 'run'", NULL);
            }
            after_separator = true;
            continue;
        }
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                return usage_error("option '%s' requires a value", arg);
            }
            cli.options.output_path = argv[++i];
            continue;
        }
        if (strcmp(arg, "--target") == 0) {
            if (i + 1 >= argc) {
                return usage_error("option '%s' requires a value", arg);
            }
            cli.options.target = argv[++i];
            continue;
        }
        if (strcmp(arg, "--cc") == 0) {
            if (i + 1 >= argc) {
                return usage_error("option '%s' requires a value", arg);
            }
            cli.options.cc = argv[++i];
            continue;
        }
        if (strcmp(arg, "--include") == 0) {
            if (i + 1 >= argc) {
                return usage_error("option '%s' requires a value", arg);
            }
            if (!push_include(&cli, argv[++i])) {
                return RYN_EXIT_USAGE;
            }
            continue;
        }
        if (strcmp(arg, "--debug") == 0) {
            cli.options.debug = true;
            cli.options.release = false;
            continue;
        }
        if (strcmp(arg, "--release") == 0) {
            cli.options.release = true;
            cli.options.debug = false;
            continue;
        }
        if (strcmp(arg, "--dump-tokens") == 0) {
            cli.options.dump_tokens = true;
            continue;
        }
        if (strcmp(arg, "--dump-ast") == 0) {
            cli.options.dump_ast = true;
            continue;
        }
        if (strcmp(arg, "--emit-ir") == 0) {
            cli.options.dump_ir = true;
            continue;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout);
            return RYN_EXIT_OK;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            return usage_error("unknown option '%s'", arg);
        }
        if (!input) {
            input = arg;
        } else if (cli.options.command == COMPILE_RUN) {
            if (!push_program_arg(&cli, arg)) {
                return RYN_EXIT_USAGE;
            }
        } else {
            return usage_error("unexpected argument '%s'", arg);
        }
    }

    if (!input) {
        fputs("ryn: error: no input file\n\n", stderr);
        print_usage(stderr);
        return RYN_EXIT_USAGE;
    }
    if (cli.options.output_path && cli.options.command != COMPILE_TRANSPILE &&
        cli.options.command != COMPILE_BUILD) {
        return usage_error("'-o' is only valid with 'transpile' and 'build'", NULL);
    }

    cli.options.input_path = input;
    cli.options.program_path = argv[0];
    cli.options.includes = cli.includes;
    cli.options.nincludes = cli.nincludes;
    cli.options.nprogram_args = (int)cli.nprogram_args;
    cli.options.program_args = cli.program_args;

    int code = compiler_execute(&cli.options);

    free(cli.includes);
    free(cli.program_args);
    return code;
}
