/*
 * Ryn programming language — compiler driver.
 *
 * Ties the stages together: source manager -> lexer -> parser -> semantic
 * analysis -> IR -> C11 backend -> native toolchain. The driver owns the
 * lifetime of every compiler stage and is the only place that touches the
 * host filesystem and spawns external processes.
 */
#ifndef RYN_COMPILER_H
#define RYN_COMPILER_H

#include "common.h"

typedef enum CompileCommand {
    COMPILE_CHECK = 0,
    COMPILE_TRANSPILE,
    COMPILE_BUILD,
    COMPILE_RUN
} CompileCommand;

typedef struct CompilerOptions {
    CompileCommand command;
    const char *input_path;  /* input .ryn file */
    const char *output_path; /* -o, may be NULL for the default */
    const char *cc;          /* C compiler executable, NULL for auto */
    const char *target;      /* "native" (only supported target) */
    const char *program_path; /* argv[0], used to locate the runtime */
    bool debug;              /* -O0 -g */
    bool release;            /* -O2 (default is debug) */

    bool dump_tokens;
    bool dump_ast;
    bool dump_ir;

    const char *const *includes;
    size_t nincludes;

    /* Additional directories searched for imported modules and packages,
     * from --module-path (repeatable). */
    const char *const *module_paths;
    size_t nmodule_paths;

    int nprogram_args;
    char *const *program_args;
} CompilerOptions;

/* Exit codes shared with main.c and documented in README/docs. */
#define RYN_EXIT_OK 0
#define RYN_EXIT_COMPILE_ERROR 1
#define RYN_EXIT_USAGE 2
#define RYN_EXIT_TOOLCHAIN 3

/* Runs one full compilation. Returns one of the RYN_EXIT_* codes. */
int compiler_execute(const CompilerOptions *options);

#endif /* RYN_COMPILER_H */
