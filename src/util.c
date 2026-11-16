#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__)
#  define _DARWIN_C_SOURCE 1
#endif

#include "common.h"

#include <ctype.h>
#include <errno.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <strings.h>
#endif

/* ------------------------------------------------------------------ */
/* Arena                                                              */
/* ------------------------------------------------------------------ */

#define ARENA_BLOCK_SIZE ((size_t)64 * 1024)
#define ARENA_ALIGN ((size_t)16)

struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    size_t cap;
    unsigned char data[1];
};

static size_t align_up(size_t n) {
    return (n + (ARENA_ALIGN - 1)) & ~(ARENA_ALIGN - 1);
}

void arena_init(Arena *arena) {
    arena->head = NULL;
}

static ArenaBlock *arena_new_block(size_t min_size) {
    size_t cap = ARENA_BLOCK_SIZE;
    if (min_size > cap) {
        cap = align_up(min_size);
    }
    ArenaBlock *block = (ArenaBlock *)malloc(sizeof(ArenaBlock) + cap);
    if (!block) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    block->next = NULL;
    block->used = 0;
    block->cap = cap;
    return block;
}

void *arena_alloc(Arena *arena, size_t size) {
    if (size == 0) {
        size = 1;
    }
    size = align_up(size);
    ArenaBlock *block = arena->head;
    if (!block || block->used + size > block->cap) {
        block = arena_new_block(size);
        block->next = arena->head;
        arena->head = block;
    }
    void *ptr = block->data + block->used;
    block->used += size;
    return ptr;
}

void *arena_calloc(Arena *arena, size_t count, size_t size) {
    size_t total;
    if (size != 0 && count > SIZE_MAX / size) {
        fprintf(stderr, "ryn: allocation overflow\n");
        abort();
    }
    total = count * size;
    void *ptr = arena_alloc(arena, total);
    memset(ptr, 0, total);
    return ptr;
}

char *arena_strdup(Arena *arena, const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *out = (char *)arena_alloc(arena, len + 1);
    memcpy(out, s, len + 1);
    return out;
}

char *arena_strndup(Arena *arena, const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len] != '\0') {
        len++;
    }
    char *out = (char *)arena_alloc(arena, len + 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

void arena_free(Arena *arena) {
    ArenaBlock *block = arena->head;
    while (block) {
        ArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena->head = NULL;
}

size_t arena_bytes_used(const Arena *arena) {
    size_t total = 0;
    for (const ArenaBlock *block = arena->head; block; block = block->next) {
        total += block->used;
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* String builder                                                     */
/* ------------------------------------------------------------------ */

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb_init(sb);
}

void sb_clear(StrBuf *sb) {
    sb->len = 0;
    if (sb->data) {
        sb->data[0] = '\0';
    }
}

static void sb_reserve(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) {
        return;
    }
    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < sb->len + extra + 1) {
        cap *= 2;
    }
    char *data = (char *)realloc(sb->data, cap);
    if (!data) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    sb->data = data;
    sb->cap = cap;
}

void sb_append_n(StrBuf *sb, const char *s, size_t n) {
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_append(StrBuf *sb, const char *s) {
    if (s) {
        sb_append_n(sb, s, strlen(s));
    }
}

void sb_append_char(StrBuf *sb, char c) {
    sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_vappendf(StrBuf *sb, const char *fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        return;
    }
    sb_reserve(sb, (size_t)needed);
    vsnprintf(sb->data + sb->len, (size_t)needed + 1, fmt, ap);
    sb->len += (size_t)needed;
}

void sb_appendf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    sb_vappendf(sb, fmt, ap);
    va_end(ap);
}

char *sb_detach(StrBuf *sb) {
    char *data = sb->data;
    if (!data) {
        data = (char *)malloc(1);
        if (!data) {
            fprintf(stderr, "ryn: out of memory\n");
            abort();
        }
        data[0] = '\0';
    }
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return data;
}

/* ------------------------------------------------------------------ */
/* Filesystem                                                         */
/* ------------------------------------------------------------------ */

char *ryn_read_file(const char *path, size_t *out_len, char *err, size_t errlen) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (err && errlen) {
            snprintf(err, errlen, "cannot open '%s': %s", path, strerror(errno));
        }
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        if (err && errlen) {
            snprintf(err, errlen, "cannot seek in '%s'", path);
        }
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        if (err && errlen) {
            snprintf(err, errlen, "cannot determine size of '%s'", path);
        }
        fclose(file);
        return NULL;
    }
    rewind(file);
    char *data = (char *)malloc((size_t)size + 1);
    if (!data) {
        if (err && errlen) {
            snprintf(err, errlen, "out of memory reading '%s'", path);
        }
        fclose(file);
        return NULL;
    }
    size_t read = fread(data, 1, (size_t)size, file);
    fclose(file);
    data[read] = '\0';
    if (out_len) {
        *out_len = read;
    }
    return data;
}

bool ryn_file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    fclose(file);
    return true;
}

bool ryn_is_directory(const char *path) {
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#endif
}

bool ryn_make_dir(const char *path) {
#if defined(_WIN32)
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0777) == 0;
#endif
}

char *ryn_getenv(const char *name) {
    const char *value = getenv(name);
    if (!value || !*value) {
        return NULL;
    }
    char *copy = (char *)malloc(strlen(value) + 1);
    if (!copy) {
        return NULL;
    }
    strcpy(copy, value);
    return copy;
}

static bool is_sep(char c) {
    return c == '/' || c == '\\';
}

char *ryn_path_join(const char *a, const char *b) {
    if (!a || !*a) {
        return ryn_strdup(b ? b : "");
    }
    if (!b || !*b) {
        return ryn_strdup(a);
    }
    size_t alen = strlen(a);
    size_t blen = strlen(b);
    bool need_sep = !is_sep(a[alen - 1]);
    char *out = (char *)malloc(alen + blen + 2);
    if (!out) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    memcpy(out, a, alen);
    size_t pos = alen;
    if (need_sep) {
        out[pos++] = '/';
    }
    memcpy(out + pos, b, blen + 1);
    return out;
}

char *ryn_path_dirname(const char *path) {
    if (!path || !*path) {
        return ryn_strdup(".");
    }
    size_t len = strlen(path);
    while (len > 1 && is_sep(path[len - 1])) {
        len--;
    }
    while (len > 0 && !is_sep(path[len - 1])) {
        len--;
    }
    while (len > 1 && is_sep(path[len - 1])) {
        len--;
    }
    if (len == 0) {
        return ryn_strdup(".");
    }
    char *out = (char *)malloc(len + 1);
    if (!out) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

char *ryn_path_basename(const char *path) {
    if (!path) {
        return ryn_strdup("");
    }
    size_t len = strlen(path);
    while (len > 0 && is_sep(path[len - 1])) {
        len--;
    }
    size_t start = len;
    while (start > 0 && !is_sep(path[start - 1])) {
        start--;
    }
    size_t n = len - start;
    char *out = (char *)malloc(n + 1);
    if (!out) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    memcpy(out, path + start, n);
    out[n] = '\0';
    return out;
}

char *ryn_path_without_ext(const char *path) {
    if (!path) {
        return ryn_strdup("");
    }
    size_t len = strlen(path);
    size_t cut = len;
    for (size_t i = len; i > 0; i--) {
        if (is_sep(path[i - 1])) {
            break;
        }
        if (path[i - 1] == '.') {
            cut = i - 1;
            break;
        }
    }
    char *out = (char *)malloc(cut + 1);
    if (!out) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    memcpy(out, path, cut);
    out[cut] = '\0';
    return out;
}

bool ryn_path_has_ext(const char *path, const char *ext) {
    size_t len = strlen(path);
    size_t elen = strlen(ext);
    if (elen + 1 > len) {
        return false;
    }
    if (path[len - elen - 1] != '.') {
        return false;
    }
#if defined(_WIN32)
    return _stricmp(path + len - elen, ext) == 0;
#else
    return strcasecmp(path + len - elen, ext) == 0;
#endif
}

char *ryn_make_temp_dir(void) {
#if defined(_WIN32)
    char base[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, base);
    if (n == 0 || n > MAX_PATH) {
        return NULL;
    }
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        char name[64];
        snprintf(name, sizeof(name), "ryn-%lu-%u", (unsigned long)GetCurrentProcessId(), attempt);
        char *path = ryn_path_join(base, name);
        if (ryn_make_dir(path)) {
            return path;
        }
        free(path);
    }
    return NULL;
#else
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) {
        tmp = "/tmp";
    }
    char *template_path = ryn_path_join(tmp, "ryn-XXXXXX");
    if (!mkdtemp(template_path)) {
        free(template_path);
        return NULL;
    }
    return template_path;
#endif
}

void ryn_remove_temp_dir(const char *path) {
    if (!path) {
        return;
    }
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    char *pattern = ryn_path_join(path, "*");
    HANDLE handle = FindFirstFileA(pattern, &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
                continue;
            }
            char *child = ryn_path_join(path, data.cFileName);
            DeleteFileA(child);
            free(child);
        } while (FindNextFileA(handle, &data));
        FindClose(handle);
    }
    free(pattern);
    RemoveDirectoryA(path);
#else
    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char *child = ryn_path_join(path, entry->d_name);
            unlink(child);
            free(child);
        }
        closedir(dir);
    }
    rmdir(path);
#endif
}

char *ryn_identifier_from(const char *s) {
    size_t len = s ? strlen(s) : 0;
    char *out = (char *)malloc(len + 2);
    if (!out) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    size_t pos = 0;
    if (len == 0) {
        out[pos++] = '_';
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == '_') {
            out[pos++] = (char)c;
        } else {
            out[pos++] = '_';
        }
    }
    out[pos] = '\0';
    if (pos > 0 && isdigit((unsigned char)out[0])) {
        memmove(out + 1, out, pos + 1);
        out[0] = '_';
    }
    return out;
}

char *ryn_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    memcpy(out, s, len + 1);
    return out;
}
