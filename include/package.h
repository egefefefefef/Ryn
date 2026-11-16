/*
 * Rynx package manifests.
 *
 * A package is described by a `rynx.toml` file using a deliberately small
 * TOML subset: sections, quoted strings, and single-line inline tables. The
 * dependency resolver and CLI live in rynx.c; this module only parses,
 * validates, and writes manifests.
 */
#ifndef RYN_PACKAGE_H
#define RYN_PACKAGE_H

#include "common.h"

#define RYNX_MANIFEST_FILE "rynx.toml"
#define RYNX_LOCK_FILE "rynx.lock"
#define RYNX_STATE_DIR ".rynx"
#define RYNX_PACKAGES_DIR ".rynx/packages"
#define RYNX_DEFAULT_ENTRY "src/main.ryn"

typedef enum RynxDepKind {
    RYNX_DEP_PATH = 0,
    RYNX_DEP_GIT = 1,
} RynxDepKind;

typedef struct RynxDep {
    const char *name;
    RynxDepKind kind;
    const char *path; /* path dependencies */
    const char *git;  /* git dependencies  */
    const char *ref;  /* tag or branch for git dependencies */
    const char *version;
} RynxDep;

RYN_DEFINE_VEC(RynxDepVec, RynxDep)

typedef struct RynxManifest {
    const char *name;
    const char *version;
    const char *entry;
    RynxDepVec deps;
} RynxManifest;

void rynx_manifest_init(RynxManifest *manifest);
void rynx_manifest_free(RynxManifest *manifest);

/* Parses manifest text. `source_name` is only used in error messages.
 * Strings are allocated in `arena`. Returns false and fills `err` on error. */
bool rynx_manifest_parse(const char *text, const char *source_name, Arena *arena,
                         RynxManifest *out, char *err, size_t errlen);
bool rynx_manifest_load(const char *path, Arena *arena, RynxManifest *out, char *err,
                        size_t errlen);
bool rynx_manifest_write(const char *path, const RynxManifest *manifest, char *err,
                         size_t errlen);

/* Writes a lock file recording resolved dependency versions. */
bool rynx_lock_write(const char *path, const RynxManifest *manifest, char *err, size_t errlen);

const RynxDep *rynx_manifest_find_dep(const RynxManifest *manifest, const char *name);
bool rynx_manifest_add_dep(RynxManifest *manifest, RynxDep dep, char *err, size_t errlen);
bool rynx_manifest_remove_dep(RynxManifest *manifest, const char *name);

/* Package names: lowercase letters, digits, '_' and '-', starting with a
 * letter or '_'. */
bool rynx_valid_package_name(const char *name);
/* Lowercases and replaces unsupported characters so `rynx init` always has a
 * usable name. Returns a malloc'd string. */
char *rynx_sanitize_package_name(const char *name);

#endif /* RYN_PACKAGE_H */
