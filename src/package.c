#include "package.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void set_error(char *err, size_t errlen, const char *fmt, ...) {
    if (!err || errlen == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
    return text;
}

/* Finds a delimiter outside a double-quoted string. */
static char *find_unquoted(char *text, char target) {
    bool in_string = false;
    bool escaped = false;
    for (char *p = text; *p; p++) {
        if (in_string && escaped) {
            escaped = false;
            continue;
        }
        if (in_string && *p == '\\') {
            escaped = true;
            continue;
        }
        if (*p == '"') {
            in_string = !in_string;
            continue;
        }
        if (!in_string && *p == target) {
            return p;
        }
    }
    return NULL;
}

static void strip_comment(char *line) {
    bool in_string = false;
    bool escaped = false;
    for (char *p = line; *p; p++) {
        if (in_string && escaped) {
            escaped = false;
            continue;
        }
        if (in_string && *p == '\\') {
            escaped = true;
            continue;
        }
        if (*p == '"') {
            in_string = !in_string;
            continue;
        }
        if (!in_string && *p == '#') {
            *p = '\0';
            return;
        }
    }
}

bool rynx_valid_package_name(const char *name) {
    if (!name || !*name) {
        return false;
    }
    unsigned char first = (unsigned char)name[0];
    if (!(islower(first) || first == '_')) {
        return false;
    }
    for (const char *p = name + 1; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(islower(c) || isdigit(c) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

char *rynx_sanitize_package_name(const char *name) {
    size_t len = name ? strlen(name) : 0;
    /* "package" is the longest fallback, and a digit prefix adds one byte. */
    size_t capacity = len + 2 < 10 ? 10 : len + 2;
    char *out = (char *)malloc(capacity);
    if (!out) {
        fprintf(stderr, "rynx: out of memory\n");
        abort();
    }
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c)) {
            out[pos++] = (char)tolower(c);
        } else if (c == '_' || c == '-') {
            out[pos++] = (char)c;
        } else if (pos > 0 && out[pos - 1] != '-') {
            out[pos++] = '-';
        }
    }
    while (pos > 0 && out[pos - 1] == '-') {
        pos--;
    }
    out[pos] = '\0';
    if (pos == 0) {
        strcpy(out, "package");
        pos = 7;
    }
    if (isdigit((unsigned char)out[0])) {
        memmove(out + 1, out, pos + 1);
        out[0] = 'p';
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Value parsing                                                      */
/* ------------------------------------------------------------------ */

static bool parse_quoted(const char *value, const char *source, int line, Arena *arena,
                         const char **out, char *err, size_t errlen) {
    const char *p = value;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"') {
        set_error(err, errlen, "%s:%d: expected a quoted string", source, line);
        return false;
    }
    p++;
    StrBuf sb;
    sb_init(&sb);
    bool closed = false;
    while (*p) {
        char c = *p++;
        if (c == '"') {
            closed = true;
            break;
        }
        if (c != '\\') {
            sb_append_char(&sb, c);
            continue;
        }
        char escaped = *p++;
        switch (escaped) {
        case '\\': sb_append_char(&sb, '\\'); break;
        case '"': sb_append_char(&sb, '"'); break;
        case 'n': sb_append_char(&sb, '\n'); break;
        case 't': sb_append_char(&sb, '\t'); break;
        case 'r': sb_append_char(&sb, '\r'); break;
        default:
            sb_free(&sb);
            set_error(err, errlen, "%s:%d: unsupported escape '\\%c'", source, line, escaped);
            return false;
        }
    }
    if (!closed) {
        sb_free(&sb);
        set_error(err, errlen, "%s:%d: unterminated string", source, line);
        return false;
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p) {
        sb_free(&sb);
        set_error(err, errlen, "%s:%d: unexpected text after string", source, line);
        return false;
    }
    *out = arena_strdup(arena, sb.data ? sb.data : "");
    sb_free(&sb);
    return true;
}

static bool parse_dep_table(char *table, const char *source, int line, Arena *arena,
                            RynxDep *dep, char *err, size_t errlen) {
    while (*table && isspace((unsigned char)*table)) {
        table++;
    }
    if (*table != '{') {
        set_error(err, errlen, "%s:%d: expected '{'", source, line);
        return false;
    }
    table++;
    char *close = find_unquoted(table, '}');
    if (!close) {
        set_error(err, errlen, "%s:%d: missing '}' in dependency table", source, line);
        return false;
    }
    *close = '\0';
    char *tail = trim(close + 1);
    if (*tail) {
        set_error(err, errlen, "%s:%d: unexpected text after dependency table", source, line);
        return false;
    }

    bool seen_ref = false;
    char *p = table;
    while (*p) {
        char *comma = find_unquoted(p, ',');
        char *part = p;
        if (comma) {
            *comma = '\0';
        }
        part = trim(part);
        if (!*part) {
            set_error(err, errlen, "%s:%d: empty entry in dependency table", source, line);
            return false;
        }
        char *eq = find_unquoted(part, '=');
        if (!eq) {
            set_error(err, errlen, "%s:%d: expected 'key = \"value\"' in dependency table",
                      source, line);
            return false;
        }
        *eq = '\0';
        char *key = trim(part);
        char *value = trim(eq + 1);
        const char *parsed = NULL;
        if (!parse_quoted(value, source, line, arena, &parsed, err, errlen)) {
            return false;
        }
        if (strcmp(key, "path") == 0) {
            if (dep->path) {
                set_error(err, errlen, "%s:%d: duplicate 'path' in dependency", source, line);
                return false;
            }
            dep->path = parsed;
        } else if (strcmp(key, "git") == 0) {
            if (dep->git) {
                set_error(err, errlen, "%s:%d: duplicate 'git' in dependency", source, line);
                return false;
            }
            dep->git = parsed;
        } else if (strcmp(key, "tag") == 0 || strcmp(key, "branch") == 0 ||
                   strcmp(key, "rev") == 0) {
            if (seen_ref) {
                set_error(err, errlen,
                          "%s:%d: only one of 'tag', 'branch' and 'rev' may be used",
                          source, line);
                return false;
            }
            dep->ref = parsed;
            seen_ref = true;
        } else {
            set_error(err, errlen, "%s:%d: unknown dependency key '%s'", source, line, key);
            return false;
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }

    if (dep->path && dep->git) {
        set_error(err, errlen, "%s:%d: dependency cannot have both 'path' and 'git'", source,
                  line);
        return false;
    }
    if (!dep->path && !dep->git) {
        set_error(err, errlen, "%s:%d: dependency needs a 'path' or 'git' key", source, line);
        return false;
    }
    if (dep->ref && !dep->git) {
        set_error(err, errlen, "%s:%d: 'tag'/'branch'/'rev' require a 'git' dependency", source,
                  line);
        return false;
    }
    dep->kind = dep->git ? RYNX_DEP_GIT : RYNX_DEP_PATH;
    return true;
}

/* ------------------------------------------------------------------ */
/* Manifest parsing                                                   */
/* ------------------------------------------------------------------ */

void rynx_manifest_init(RynxManifest *manifest) {
    memset(manifest, 0, sizeof(*manifest));
    RynxDepVec_init(&manifest->deps);
}

void rynx_manifest_free(RynxManifest *manifest) {
    if (!manifest) {
        return;
    }
    RynxDepVec_free(&manifest->deps);
    manifest->name = NULL;
    manifest->version = NULL;
    manifest->entry = NULL;
}

const RynxDep *rynx_manifest_find_dep(const RynxManifest *manifest, const char *name) {
    for (size_t i = 0; i < manifest->deps.len; i++) {
        if (strcmp(manifest->deps.items[i].name, name) == 0) {
            return &manifest->deps.items[i];
        }
    }
    return NULL;
}

typedef enum ManifestSection {
    SECTION_NONE = 0,
    SECTION_PACKAGE,
    SECTION_DEPENDENCIES,
} ManifestSection;

bool rynx_manifest_parse(const char *text, const char *source_name, Arena *arena,
                         RynxManifest *out, char *err, size_t errlen) {
    rynx_manifest_init(out);
    const char *source = source_name ? source_name : "rynx.toml";
    char *copy = arena_strndup(arena, text ? text : "", text ? strlen(text) : 0);
    char *cursor = copy;
    int line_no = 0;
    ManifestSection section = SECTION_NONE;

    while (cursor) {
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        if (newline) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor = NULL;
        }
        line_no++;
        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line[line_len - 1] = '\0';
        }
        strip_comment(line);
        line = trim(line);
        if (!*line) {
            continue;
        }

        if (*line == '[') {
            char *close = strchr(line, ']');
            if (!close) {
                set_error(err, errlen, "%s:%d: missing ']' in section header", source, line_no);
                goto fail;
            }
            *close = '\0';
            char *name = trim(line + 1);
            char *tail = trim(close + 1);
            if (*tail) {
                set_error(err, errlen, "%s:%d: unexpected text after section header", source,
                          line_no);
                goto fail;
            }
            if (strcmp(name, "package") == 0) {
                section = SECTION_PACKAGE;
            } else if (strcmp(name, "dependencies") == 0) {
                section = SECTION_DEPENDENCIES;
            } else {
                set_error(err, errlen, "%s:%d: unknown section '[%s]'", source, line_no, name);
                goto fail;
            }
            continue;
        }

        char *eq = find_unquoted(line, '=');
        if (!eq) {
            set_error(err, errlen, "%s:%d: expected 'key = value'", source, line_no);
            goto fail;
        }
        *eq = '\0';
        char *key = trim(line);
        char *value = trim(eq + 1);
        if (!*key) {
            set_error(err, errlen, "%s:%d: empty key", source, line_no);
            goto fail;
        }
        if (section == SECTION_NONE) {
            set_error(err, errlen, "%s:%d: key '%s' is outside a section", source, line_no, key);
            goto fail;
        }

        if (section == SECTION_PACKAGE) {
            const char *parsed = NULL;
            if (strcmp(key, "name") != 0 && strcmp(key, "version") != 0 &&
                strcmp(key, "entry") != 0) {
                set_error(err, errlen, "%s:%d: unknown package key '%s'", source, line_no, key);
                goto fail;
            }
            if (!parse_quoted(value, source, line_no, arena, &parsed, err, errlen)) {
                goto fail;
            }
            const char **slot = NULL;
            if (strcmp(key, "name") == 0) {
                slot = &out->name;
            } else if (strcmp(key, "version") == 0) {
                slot = &out->version;
            } else {
                slot = &out->entry;
            }
            if (*slot) {
                set_error(err, errlen, "%s:%d: duplicate package key '%s'", source, line_no, key);
                goto fail;
            }
            *slot = parsed;
            continue;
        }

        if (!rynx_valid_package_name(key)) {
            set_error(err, errlen,
                      "%s:%d: invalid dependency name '%s' (use lowercase letters, digits, "
                      "'_' or '-')",
                      source, line_no, key);
            goto fail;
        }
        if (rynx_manifest_find_dep(out, key)) {
            set_error(err, errlen, "%s:%d: dependency '%s' is declared twice", source, line_no,
                      key);
            goto fail;
        }
        RynxDep dep;
        memset(&dep, 0, sizeof(dep));
        dep.name = arena_strdup(arena, key);
        if (*value == '"') {
            const char *path = NULL;
            if (!parse_quoted(value, source, line_no, arena, &path, err, errlen)) {
                goto fail;
            }
            dep.kind = RYNX_DEP_PATH;
            dep.path = path;
        } else if (*value == '{') {
            if (!parse_dep_table(value, source, line_no, arena, &dep, err, errlen)) {
                goto fail;
            }
        } else {
            set_error(err, errlen,
                      "%s:%d: dependency value must be a quoted path or an inline table", source,
                      line_no);
            goto fail;
        }
        RynxDepVec_push(&out->deps, dep);
    }

    if (!out->name) {
        set_error(err, errlen, "%s: missing '[package] name'", source);
        goto fail;
    }
    if (!rynx_valid_package_name(out->name)) {
        set_error(err, errlen,
                  "%s: invalid package name '%s' (use lowercase letters, digits, '_' or '-')",
                  source, out->name);
        goto fail;
    }
    if (!out->version) {
        set_error(err, errlen, "%s: missing '[package] version'", source);
        goto fail;
    }
    if (!out->entry) {
        out->entry = arena_strdup(arena, RYNX_DEFAULT_ENTRY);
    }
    return true;

fail:
    rynx_manifest_free(out);
    return false;
}

bool rynx_manifest_load(const char *path, Arena *arena, RynxManifest *out, char *err,
                        size_t errlen) {
    size_t length = 0;
    char read_error[256];
    char *text = ryn_read_file(path, &length, read_error, sizeof(read_error));
    if (!text) {
        set_error(err, errlen, "cannot read '%s': %s", path,
                  read_error[0] ? read_error : "unknown error");
        return false;
    }
    bool ok = rynx_manifest_parse(text, path, arena, out, err, errlen);
    free(text);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Manifest writing                                                   */
/* ------------------------------------------------------------------ */

static bool write_quoted(FILE *out, const char *value) {
    if (fputc('"', out) == EOF) {
        return false;
    }
    for (const char *p = value; *p; p++) {
        switch (*p) {
        case '\\': if (fputs("\\\\", out) == EOF) return false; break;
        case '"': if (fputs("\\\"", out) == EOF) return false; break;
        case '\n': if (fputs("\\n", out) == EOF) return false; break;
        case '\t': if (fputs("\\t", out) == EOF) return false; break;
        case '\r': if (fputs("\\r", out) == EOF) return false; break;
        default:
            if (fputc((unsigned char)*p, out) == EOF) {
                return false;
            }
            break;
        }
    }
    return fputc('"', out) != EOF;
}

static bool write_manifest_body(FILE *out, const RynxManifest *manifest) {
    fputs("# Generated by rynx. See `rynx help`.\n\n[package]\n", out);
    fputs("name = ", out);
    if (!write_quoted(out, manifest->name)) {
        return false;
    }
    fputs("\nversion = ", out);
    if (!write_quoted(out, manifest->version)) {
        return false;
    }
    fputs("\nentry = ", out);
    if (!write_quoted(out, manifest->entry)) {
        return false;
    }
    fputs("\n\n[dependencies]\n", out);
    for (size_t i = 0; i < manifest->deps.len; i++) {
        const RynxDep *dep = &manifest->deps.items[i];
        fputs(dep->name, out);
        fputs(" = { ", out);
        if (dep->kind == RYNX_DEP_PATH) {
            fputs("path = ", out);
            if (!write_quoted(out, dep->path)) {
                return false;
            }
        } else {
            fputs("git = ", out);
            if (!write_quoted(out, dep->git)) {
                return false;
            }
            if (dep->ref) {
                fputs(", tag = ", out);
                if (!write_quoted(out, dep->ref)) {
                    return false;
                }
            }
        }
        fputs(" }\n", out);
    }
    return ferror(out) == 0;
}

bool rynx_manifest_write(const char *path, const RynxManifest *manifest, char *err,
                         size_t errlen) {
    FILE *out = fopen(path, "wb");
    if (!out) {
        set_error(err, errlen, "cannot write '%s': %s", path, strerror(errno));
        return false;
    }
    bool ok = write_manifest_body(out, manifest);
    if (fclose(out) != 0) {
        ok = false;
    }
    if (!ok) {
        set_error(err, errlen, "failed to write '%s'", path);
    }
    return ok;
}

bool rynx_lock_write(const char *path, const RynxManifest *manifest, char *err,
                     size_t errlen) {
    FILE *out = fopen(path, "wb");
    if (!out) {
        set_error(err, errlen, "cannot write '%s': %s", path, strerror(errno));
        return false;
    }
    fputs("# Generated by rynx. Do not edit.\nversion = 1\n", out);
    for (size_t i = 0; i < manifest->deps.len; i++) {
        const RynxDep *dep = &manifest->deps.items[i];
        fputs("\n[[package]]\nname = ", out);
        if (!write_quoted(out, dep->name)
            || fputs("\nsource = ", out) == EOF
            || !write_quoted(out, dep->kind == RYNX_DEP_GIT ? "git" : "path")) {
            goto write_fail;
        }
        if (dep->kind == RYNX_DEP_PATH) {
            if (fputs("\npath = ", out) == EOF || !write_quoted(out, dep->path)) {
                goto write_fail;
            }
        } else {
            if (fputs("\ngit = ", out) == EOF || !write_quoted(out, dep->git)) {
                goto write_fail;
            }
            if (dep->ref && (fputs("\nref = ", out) == EOF || !write_quoted(out, dep->ref))) {
                goto write_fail;
            }
        }
        if (dep->version && (fputs("\nversion = ", out) == EOF
                             || !write_quoted(out, dep->version))) {
            goto write_fail;
        }
        fputc('\n', out);
    }
    if (fclose(out) != 0) {
        set_error(err, errlen, "failed to write '%s'", path);
        return false;
    }
    return true;

write_fail:
    fclose(out);
    set_error(err, errlen, "failed to write '%s'", path);
    return false;
}

/* ------------------------------------------------------------------ */
/* Editing                                                           */
/* ------------------------------------------------------------------ */

bool rynx_manifest_add_dep(RynxManifest *manifest, RynxDep dep, char *err, size_t errlen) {
    if (!rynx_valid_package_name(dep.name)) {
        set_error(err, errlen, "invalid dependency name '%s'", dep.name ? dep.name : "");
        return false;
    }
    if (rynx_manifest_find_dep(manifest, dep.name)) {
        set_error(err, errlen, "dependency '%s' is already declared", dep.name);
        return false;
    }
    if (manifest->name && strcmp(manifest->name, dep.name) == 0) {
        set_error(err, errlen, "a package cannot depend on itself");
        return false;
    }
    RynxDepVec_push(&manifest->deps, dep);
    return true;
}

bool rynx_manifest_remove_dep(RynxManifest *manifest, const char *name) {
    for (size_t i = 0; i < manifest->deps.len; i++) {
        if (strcmp(manifest->deps.items[i].name, name) == 0) {
            memmove(&manifest->deps.items[i], &manifest->deps.items[i + 1],
                    (manifest->deps.len - i - 1) * sizeof(RynxDep));
            manifest->deps.len--;
            return true;
        }
    }
    return false;
}
