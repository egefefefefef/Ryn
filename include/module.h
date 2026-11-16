/*
 * Ryn programming language — user modules.
 *
 * Loads every source file that takes part in a compilation, resolves imports
 * (local modules, `--module-path` directories, and `.rynx/packages`), and
 * merges the resulting ASTs into a single program that later compiler stages
 * can analyze without knowing about files.
 *
 * Modules are namespaced by their import alias. Top-level declarations from a
 * user module keep their source names in diagnostics but are qualified as
 * `alias.name` before semantic analysis, which is what lets two packages
 * declare the same struct or function name.
 */
#ifndef RYN_MODULE_H
#define RYN_MODULE_H

#include "ast.h"
#include "diagnostics.h"
#include "source.h"

typedef struct ModuleOptions {
    /* Directories searched for modules and packages, in order. These are the
     * values passed with `--module-path` (repeatable). */
    const char *const *search_paths;
    size_t nsearch_paths;
} ModuleOptions;

/* Loads `entry_path`, resolves its imports recursively, and returns a merged
 * program in `*out_program`. Returns false when a file cannot be read, parsing
 * failed, an import cycle was found, or an imported module does not exist.
 * `dump_tokens` prints the token stream of every loaded file. */
bool modules_load_program(Arena *arena, Diagnostics *diag, SourceManager *sm,
                          const char *entry_path, const ModuleOptions *options,
                          bool dump_tokens, AstProgram **out_program);

#endif /* RYN_MODULE_H */
