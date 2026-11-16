/*
 * Ryn programming language — common utilities.
 *
 * Arena allocation, string builder, dynamic vectors, filesystem helpers.
 * Everything here is plain C17 and has no dependency on the rest of the
 * compiler.
 */
#ifndef RYN_COMMON_H
#define RYN_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#  define RYN_NORETURN __declspec(noreturn)
#  define RYN_PRINTF_FMT(x, y)
#else
#  define RYN_NORETURN __attribute__((noreturn))
#  define RYN_PRINTF_FMT(x, y) __attribute__((format(printf, x, y)))
#endif

#define RYN_UNUSED(x) ((void)(x))
#define RYN_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------------ */
/* Arena allocator                                                    */
/* ------------------------------------------------------------------ */

typedef struct ArenaBlock ArenaBlock;

typedef struct Arena {
    ArenaBlock *head;
} Arena;

void arena_init(Arena *arena);
void *arena_alloc(Arena *arena, size_t size);
void *arena_calloc(Arena *arena, size_t count, size_t size);
char *arena_strdup(Arena *arena, const char *s);
char *arena_strndup(Arena *arena, const char *s, size_t n);
void arena_free(Arena *arena);
size_t arena_bytes_used(const Arena *arena);

/* Allocates a zeroed array of pointers / structs inside the arena. */
#define ARENA_ARRAY(arena, T, count) \
    ((T *)arena_calloc((arena), (count), sizeof(T)))

/* ------------------------------------------------------------------ */
/* String builder                                                     */
/* ------------------------------------------------------------------ */

typedef struct StrBuf {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_clear(StrBuf *sb);
void sb_append(StrBuf *sb, const char *s);
void sb_append_n(StrBuf *sb, const char *s, size_t n);
void sb_append_char(StrBuf *sb, char c);
void sb_appendf(StrBuf *sb, const char *fmt, ...) RYN_PRINTF_FMT(2, 3);
void sb_vappendf(StrBuf *sb, const char *fmt, va_list ap);
char *sb_detach(StrBuf *sb); /* transfers ownership to caller (malloc) */

/* ------------------------------------------------------------------ */
/* Dynamic vectors                                                    */
/* ------------------------------------------------------------------ */

#define RYN_DEFINE_VEC(Name, T)                                                 \
    typedef struct Name {                                                       \
        T *items;                                                               \
        size_t len;                                                             \
        size_t cap;                                                             \
    } Name;                                                                     \
    static inline void Name##_init(Name *v) {                                   \
        v->items = NULL;                                                        \
        v->len = 0;                                                             \
        v->cap = 0;                                                             \
    }                                                                           \
    static inline void Name##_push(Name *v, T item) {                           \
        if (v->len == v->cap) {                                                 \
            size_t cap = v->cap ? v->cap * 2 : 8;                               \
            T *items = (T *)realloc(v->items, cap * sizeof(T));                  \
            if (!items) {                                                       \
                fprintf(stderr, "ryn: out of memory\n");                         \
                abort();                                                        \
            }                                                                   \
            v->items = items;                                                   \
            v->cap = cap;                                                       \
        }                                                                       \
        v->items[v->len++] = item;                                              \
    }                                                                           \
    static inline void Name##_free(Name *v) {                                   \
        free(v->items);                                                         \
        v->items = NULL;                                                        \
        v->len = 0;                                                             \
        v->cap = 0;                                                             \
    }

/* ------------------------------------------------------------------ */
/* Filesystem helpers                                                 */
/* ------------------------------------------------------------------ */

/* malloc'd copy of a string (aborts on out of memory). */
char *ryn_strdup(const char *s);

/* Reads a whole file. Returns NULL on failure and fills err (if non-NULL). */
char *ryn_read_file(const char *path, size_t *out_len, char *err, size_t errlen);
bool ryn_file_exists(const char *path);
bool ryn_is_directory(const char *path);
bool ryn_make_dir(const char *path);
char *ryn_getenv(const char *name); /* malloc'd copy or NULL */
char *ryn_path_join(const char *a, const char *b);
/* Absolute, normalized path. Falls back to a lexical absolute path when the
 * file does not exist. Returns a malloc'd string or NULL. */
char *ryn_path_canonical(const char *path);
/* Current working directory as a malloc'd string, or NULL. */
char *ryn_current_dir(void);
char *ryn_path_dirname(const char *path);
char *ryn_path_basename(const char *path);
char *ryn_path_without_ext(const char *path);
bool ryn_path_has_ext(const char *path, const char *ext);
/* Creates a fresh temporary directory. Returns malloc'd path or NULL. */
char *ryn_make_temp_dir(void);
/* Recursive removal of a file or directory tree. Missing paths are ignored. */
void ryn_remove_tree(const char *path);
/* Best effort recursive removal of a directory created by ryn_make_temp_dir. */
void ryn_remove_temp_dir(const char *path);

/* Runs argv[0] with argv and waits for it. Returns the exit code, or -1 when
 * the process could not be started. */
int ryn_spawn_process(char *const argv[]);

/* Turns an arbitrary string into a valid identifier fragment. */
char *ryn_identifier_from(const char *s);

#endif /* RYN_COMMON_H */
