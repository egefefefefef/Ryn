/*
 * Ryn programming language — diagnostics.
 *
 * Every error and warning carries a stable code (E0001, W0001, ...), a
 * source position and an optional note. Rendering produces a snippet of the
 * offending source line with a caret, in the style:
 *
 *   error[E0001]: undefined variable 'value'
 *     --> main.ryn:5:11
 *      |
 *    5 |     print(value)
 *      |           ^^^^^
 *      |
 *      = variable 'value' was not declared in this scope
 */
#ifndef RYN_DIAGNOSTICS_H
#define RYN_DIAGNOSTICS_H

#include "common.h"
#include "source.h"

typedef enum DiagCode {
    DIAG_NONE = 0,

    /* Errors. The numeric value matches the public code (E0001 -> 1). */
    E_UNDEFINED_VARIABLE = 1,
    E_UNDEFINED_FUNCTION = 2,
    E_TYPE_MISMATCH = 3,
    E_IMMUTABLE_ASSIGN = 4,
    E_ARG_COUNT = 5,
    E_ARG_TYPE = 6,
    E_UNKNOWN_TYPE = 7,
    E_DUPLICATE = 8,
    E_SYNTAX = 9,
    E_INDENT = 10,
    E_BAD_OPERATOR = 11,
    E_MISSING_RETURN = 12,
    E_DEREF_NON_POINTER = 13,
    E_INVALID_ADDRESS_OF = 14,
    E_VOID_VALUE = 15,
    E_BREAK_OUTSIDE_LOOP = 16,
    E_RETURN_OUTSIDE_FN = 17,
    E_UNKNOWN_FIELD = 18,
    E_MISSING_MAIN = 19,
    E_NOT_CALLABLE = 20,
    E_BAD_CAST = 21,
    E_DUPLICATE_FIELD = 22,
    E_NOT_A_STRUCT = 23,
    E_BAD_INDEX = 24,
    E_LITERAL_RANGE = 25,
    E_UNKNOWN_MODULE = 26,
    E_NOT_A_MODULE = 27,
    E_RESERVED_NAME = 28,
    E_UNINITIALIZED = 29,
    E_BAD_MAIN = 30,
    E_NON_CONSTANT = 31,
    E_ASSIGN_TARGET = 32,
    E_IMPORT_POSITION = 33,
    E_NO_INPUT = 34,
    E_OUT_OF_RANGE = 35,
    E_STATIC_INIT = 36,
    E_UNSUPPORTED = 37,
    E_IO = 38,
    E_INTERNAL = 39,

    /* Warnings. */
    W_UNUSED_VARIABLE = 101,
    W_UNREACHABLE = 102,
    W_SHADOWED = 103,
    W_DIVISION_BY_ZERO = 104,
    W_UNUSED_RESULT = 105,

    DIAG_CODE_MAX = 200
} DiagCode;

typedef struct Diagnostic {
    DiagCode code;
    SourcePos pos;
    int span; /* caret length, in bytes (>= 1) */
    bool is_warning;
    char message[512];
    char note[768];
} Diagnostic;

RYN_DEFINE_VEC(DiagnosticVec, Diagnostic)

typedef struct Diagnostics {
    DiagnosticVec items;
    int error_count;
    int warning_count;
    bool color;
    int max_errors; /* abort compilation once exceeded (0 = unlimited) */
} Diagnostics;

void diag_init(Diagnostics *d);
void diag_free(Diagnostics *d);
void diag_set_color(Diagnostics *d, bool enabled);

void diag_error(Diagnostics *d, SourcePos pos, DiagCode code, const char *fmt, ...)
    RYN_PRINTF_FMT(4, 5);
void diag_error_span(Diagnostics *d, SourcePos pos, int span, DiagCode code,
                     const char *fmt, ...) RYN_PRINTF_FMT(5, 6);
void diag_error_note(Diagnostics *d, SourcePos pos, int span, DiagCode code,
                     const char *note, const char *fmt, ...) RYN_PRINTF_FMT(6, 7);
void diag_warn(Diagnostics *d, SourcePos pos, DiagCode code, const char *fmt, ...)
    RYN_PRINTF_FMT(4, 5);
void diag_warn_span(Diagnostics *d, SourcePos pos, int span, DiagCode code,
                    const char *fmt, ...) RYN_PRINTF_FMT(5, 6);
void diag_warn_note(Diagnostics *d, SourcePos pos, int span, DiagCode code,
                    const char *note, const char *fmt, ...) RYN_PRINTF_FMT(6, 7);

bool diag_has_errors(const Diagnostics *d);
bool diag_too_many_errors(const Diagnostics *d);
const char *diag_code_string(DiagCode code);
const char *diag_severity_name(const Diagnostic *diag);

/* Prints every diagnostic to out, including a source excerpt when the
 * SourceManager still owns the file. */
void diag_render_all(const Diagnostics *d, const SourceManager *sm, FILE *out);

#endif /* RYN_DIAGNOSTICS_H */
