/*
 * Ryn programming language — source manager.
 *
 * Owns every source file that takes part in a compilation, keeps the raw
 * bytes plus a line index, and provides source positions that diagnostics
 * and the lexer use.
 */
#ifndef RYN_SOURCE_H
#define RYN_SOURCE_H

#include "common.h"

/* A position inside a source file. line and column are 1-based. file is the
 * index of the SourceFile in the SourceManager (0 for the main file).
 * offset is a byte offset into the file used for fast slicing. */
typedef struct SourcePos {
    int file;
    int line;
    int column;
    size_t offset;
} SourcePos;

typedef struct SourceFile {
    int id;
    char *path;   /* display path (as given by the user) */
    char *data;   /* NUL terminated file contents */
    size_t len;
    size_t *line_starts;
    int line_count;
} SourceFile;

RYN_DEFINE_VEC(SourceFileVec, SourceFile *)

typedef struct SourceManager {
    Arena *arena;
    SourceFileVec files;
} SourceManager;

void sm_init(SourceManager *sm, Arena *arena);
void sm_free(SourceManager *sm);

/* Takes ownership of data (malloc'd, NUL terminated). */
SourceFile *sm_add(SourceManager *sm, const char *path, char *data, size_t len);
/* Reads the file from disk. Returns NULL on failure and fills err. */
SourceFile *sm_load_file(SourceManager *sm, const char *path, char *err, size_t errlen);
SourceFile *sm_file(SourceManager *sm, int id);
SourceFile *sm_main_file(SourceManager *sm);

/* Returns a pointer into the file for the given 1-based line, and stores the
 * line length (without the newline) in out_len. Returns NULL for bad lines. */
const char *sm_line(const SourceFile *file, int line, size_t *out_len);

/* Builds a SourcePos from a byte offset (used by tests and tooling). */
SourcePos sm_pos_from_offset(const SourceFile *file, size_t offset);

#endif /* RYN_SOURCE_H */
