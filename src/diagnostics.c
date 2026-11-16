#include "diagnostics.h"

static void diag_push(Diagnostics *d, DiagCode code, SourcePos pos, int span, bool warn,
                      const char *note, const char *fmt, va_list ap) {
    if (warn) {
        d->warning_count++;
    } else {
        d->error_count++;
    }
    Diagnostic diag;
    memset(&diag, 0, sizeof(diag));
    diag.code = code;
    diag.pos = pos;
    diag.span = span > 0 ? span : 1;
    diag.is_warning = warn;
    vsnprintf(diag.message, sizeof(diag.message), fmt, ap);
    if (note) {
        snprintf(diag.note, sizeof(diag.note), "%s", note);
    }
    DiagnosticVec_push(&d->items, diag);
}

void diag_init(Diagnostics *d) {
    DiagnosticVec_init(&d->items);
    d->error_count = 0;
    d->warning_count = 0;
    d->color = false;
    d->max_errors = 100;
}

void diag_free(Diagnostics *d) {
    DiagnosticVec_free(&d->items);
}

void diag_set_color(Diagnostics *d, bool enabled) {
    d->color = enabled;
}

void diag_error(Diagnostics *d, SourcePos pos, DiagCode code, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_push(d, code, pos, 1, false, NULL, fmt, ap);
    va_end(ap);
}

void diag_error_span(Diagnostics *d, SourcePos pos, int span, DiagCode code, const char *fmt,
                     ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_push(d, code, pos, span, false, NULL, fmt, ap);
    va_end(ap);
}

void diag_error_note(Diagnostics *d, SourcePos pos, int span, DiagCode code, const char *note,
                     const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_push(d, code, pos, span, false, note, fmt, ap);
    va_end(ap);
}

void diag_warn(Diagnostics *d, SourcePos pos, DiagCode code, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_push(d, code, pos, 1, true, NULL, fmt, ap);
    va_end(ap);
}

void diag_warn_span(Diagnostics *d, SourcePos pos, int span, DiagCode code, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_push(d, code, pos, span, true, NULL, fmt, ap);
    va_end(ap);
}

void diag_warn_note(Diagnostics *d, SourcePos pos, int span, DiagCode code, const char *note,
                    const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_push(d, code, pos, span, true, note, fmt, ap);
    va_end(ap);
}

bool diag_has_errors(const Diagnostics *d) {
    return d->error_count > 0;
}

bool diag_too_many_errors(const Diagnostics *d) {
    return d->max_errors > 0 && d->error_count >= d->max_errors;
}

const char *diag_code_string(DiagCode code) {
    static char buffer[8][16];
    static unsigned slot = 0;
    char *out = buffer[slot];
    slot = (slot + 1) % 8;
    if (code == DIAG_NONE) {
        snprintf(out, 16, "E0000");
    } else if ((int)code >= 100) {
        snprintf(out, 16, "W%04d", (int)code - 100);
    } else {
        snprintf(out, 16, "E%04d", (int)code);
    }
    return out;
}

const char *diag_severity_name(const Diagnostic *diag) {
    return diag->is_warning ? "warning" : "error";
}

static void print_excerpt(const Diagnostic *diag, const SourceManager *sm, FILE *out,
                          const char *color_sev, const char *color_reset, const char *color_note) {
    if (!sm) {
        return;
    }
    const SourceFile *file = sm_file((SourceManager *)sm, diag->pos.file);
    if (!file) {
        return;
    }
    size_t line_len = 0;
    const char *line = sm_line(file, diag->pos.line, &line_len);
    if (!line) {
        return;
    }

    /* Long lines are clipped around the caret. */
    size_t window_start = 0;
    size_t window_len = line_len;
    int shown_col = diag->pos.column;
    if (line_len > 100) {
        size_t col = diag->pos.column > 0 ? (size_t)diag->pos.column - 1 : 0;
        if (col > 60) {
            window_start = col - 60;
        }
        window_len = line_len - window_start;
        if (window_len > 100) {
            window_len = 100;
        }
        shown_col = (int)(col - window_start) + 1;
    }

    int number = diag->pos.line;
    int digits = 1;
    for (int n = number; n >= 10; n /= 10) {
        digits++;
    }
    fprintf(out, "%*s--> %s:%d:%d\n", digits + 2, "", file->path, diag->pos.line, diag->pos.column);
    fprintf(out, "%*s |\n", digits + 2, "");

    if (window_start > 0) {
        fprintf(out, "%*d | ...", digits, number);
    } else {
        fprintf(out, "%*d | ", digits, number);
    }
    fwrite(line + window_start, 1, window_len, out);
    fputc('\n', out);

    fprintf(out, "%*s | ", digits, "");
    for (int i = 1; i < shown_col; i++) {
        char c = (size_t)(i - 1) < window_len ? line[window_start + (size_t)(i - 1)] : ' ';
        fputc(c == '\t' ? '\t' : ' ', out);
    }
    fputs(color_sev, out);
    int span = diag->span > 0 ? diag->span : 1;
    if ((size_t)(shown_col - 1) + (size_t)span > window_len) {
        span = (int)(window_len - (size_t)(shown_col - 1));
        if (span < 1) {
            span = 1;
        }
    }
    for (int i = 0; i < span; i++) {
        fputc('^', out);
    }
    fputs(color_reset, out);
    fputc('\n', out);

    if (diag->note[0]) {
        fprintf(out, "%*s |\n", digits + 2, "");
        fprintf(out, "%*s = %s%s%s\n", digits + 2, "", color_note, diag->note, color_reset);
    }
}

void diag_render_all(const Diagnostics *d, const SourceManager *sm, FILE *out) {
    const char *bold = d->color ? "\033[1m" : "";
    const char *red = d->color ? "\033[1;31m" : "";
    const char *yellow = d->color ? "\033[1;33m" : "";
    const char *cyan = d->color ? "\033[1;36m" : "";
    const char *reset = d->color ? "\033[0m" : "";

    for (size_t i = 0; i < d->items.len; i++) {
        const Diagnostic *diag = &d->items.items[i];
        const char *sev_color = diag->is_warning ? yellow : red;
        const char *code = diag_code_string(diag->code);
        fprintf(out, "%s%s[%s]%s%s: %s%s%s\n", sev_color, diag_severity_name(diag), code, reset,
                bold, diag->message, reset, "");
        if (diag->pos.file >= 0) {
            print_excerpt(diag, sm, out, sev_color, reset, cyan);
        }
        if (i + 1 < d->items.len) {
            fputc('\n', out);
        }
    }
    if (d->error_count > 0) {
        int errors = d->error_count;
        int warnings = d->warning_count;
        fprintf(out, "\n%serror%s: could not compile", red, reset);
        if (errors == 1) {
            fputs(" due to 1 previous error", out);
        } else {
            fprintf(out, " due to %d previous errors", errors);
        }
        if (warnings == 1) {
            fputs(" (1 warning emitted)", out);
        } else if (warnings > 1) {
            fprintf(out, " (%d warnings emitted)", warnings);
        }
        fputc('\n', out);
    }
}
