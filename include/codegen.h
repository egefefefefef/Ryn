/*
 * Ryn programming language — C11 backend.
 *
 * Consumes the IR (and the declaration table of the AST) and emits a
 * standalone C11 translation unit. The generated file only relies on the Ryn
 * runtime and the C standard library.
 */
#ifndef RYN_CODEGEN_H
#define RYN_CODEGEN_H

#include "ast.h"
#include "diagnostics.h"
#include "ir.h"
#include "types.h"

typedef struct CodegenOptions {
    const char *source_path; /* used in the header comment */
    bool debug;              /* adds a note and keeps bounds checks explicit */
    const char *const *includes;
    size_t nincludes;
} CodegenOptions;

/* Emits the translation unit. Returns false when an internal limitation was
 * hit (a diagnostic is emitted in that case). */
bool codegen_emit(FILE *out, Arena *arena, Diagnostics *diag, AstProgram *program, IrModule *module,
                  TypeContext *types, const CodegenOptions *options);

#endif /* RYN_CODEGEN_H */
