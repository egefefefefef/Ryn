#include "source.h"

void sm_init(SourceManager *sm, Arena *arena) {
    sm->arena = arena;
    SourceFileVec_init(&sm->files);
}

void sm_free(SourceManager *sm) {
    for (size_t i = 0; i < sm->files.len; i++) {
        SourceFile *file = sm->files.items[i];
        free(file->path);
        free(file->data);
        free(file->line_starts);
        free(file);
    }
    SourceFileVec_free(&sm->files);
}

static void build_line_index(SourceFile *file) {
    int capacity = 16;
    file->line_starts = (size_t *)malloc(sizeof(size_t) * (size_t)capacity);
    if (!file->line_starts) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    int count = 0;
    file->line_starts[count++] = 0;
    for (size_t i = 0; i < file->len; i++) {
        if (file->data[i] == '\n') {
            if (count == capacity) {
                capacity *= 2;
                size_t *grown =
                    (size_t *)realloc(file->line_starts, sizeof(size_t) * (size_t)capacity);
                if (!grown) {
                    fprintf(stderr, "ryn: out of memory\n");
                    abort();
                }
                file->line_starts = grown;
            }
            file->line_starts[count++] = i + 1;
        }
    }
    file->line_count = count;
}

SourceFile *sm_add(SourceManager *sm, const char *path, char *data, size_t len) {
    SourceFile *file = (SourceFile *)calloc(1, sizeof(SourceFile));
    if (!file) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    file->id = (int)sm->files.len;
    file->path = ryn_strdup(path ? path : "<input>");
    file->data = data;
    file->len = len;
    build_line_index(file);
    SourceFileVec_push(&sm->files, file);
    return file;
}

SourceFile *sm_load_file(SourceManager *sm, const char *path, char *err, size_t errlen) {
    size_t len = 0;
    char local_err[512];
    char *data = ryn_read_file(path, &len, local_err, sizeof(local_err));
    if (!data) {
        if (err && errlen) {
            snprintf(err, errlen, "%s", local_err);
        }
        return NULL;
    }
    return sm_add(sm, path, data, len);
}

SourceFile *sm_file(SourceManager *sm, int id) {
    if (id < 0 || (size_t)id >= sm->files.len) {
        return NULL;
    }
    return sm->files.items[id];
}

SourceFile *sm_main_file(SourceManager *sm) {
    if (sm->files.len == 0) {
        return NULL;
    }
    return sm->files.items[0];
}

const char *sm_line(const SourceFile *file, int line, size_t *out_len) {
    if (!file || line < 1 || line > file->line_count) {
        return NULL;
    }
    size_t start = file->line_starts[line - 1];
    size_t end = (line < file->line_count) ? file->line_starts[line] : file->len;
    while (end > start && (file->data[end - 1] == '\n' || file->data[end - 1] == '\r')) {
        end--;
    }
    if (out_len) {
        *out_len = end - start;
    }
    return file->data + start;
}

SourcePos sm_pos_from_offset(const SourceFile *file, size_t offset) {
    SourcePos pos = {0, 1, 1, offset};
    if (!file) {
        return pos;
    }
    pos.file = file->id;
    if (offset > file->len) {
        offset = file->len;
    }
    int line = 1;
    size_t line_start = 0;
    for (int i = 0; i < file->line_count; i++) {
        if (file->line_starts[i] <= offset) {
            line = i + 1;
            line_start = file->line_starts[i];
        } else {
            break;
        }
    }
    pos.line = line;
    pos.column = (int)(offset - line_start) + 1;
    return pos;
}
