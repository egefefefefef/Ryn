#include "module.h"

#include "builtins.h"
#include "lexer.h"
#include "package.h"
#include "parser.h"

typedef struct ModuleUnit ModuleUnit;

typedef enum ModuleImportKind {
    MODULE_IMPORT_BUILTIN = 0,
    MODULE_IMPORT_USER,
    MODULE_IMPORT_UNRESOLVED
} ModuleImportKind;

typedef struct ModuleImport {
    const char *alias;
    SourcePos pos;
    ModuleImportKind kind;
    ModuleUnit *unit; /* MODULE_IMPORT_USER only */
} ModuleImport;

/* A `path` dependency from the manifest closest to a source file. The import
 * name is the dependency name, not necessarily its directory name. */
typedef struct PathDependency {
    const char *name;
    const char *dir;
} PathDependency;

struct ModuleUnit {
    const char *path;      /* canonical file path */
    const char *dir;       /* dirname(path) */
    const char *alias;     /* source-level import name; NULL for the entry module */
    const char *qualifier; /* unique name used for merged symbols; NULL for the entry */
    SourceFile *file;
    AstProgram *ast;
    ModuleImport *imports;
    size_t nimports;
    size_t import_capacity;
    PathDependency *path_deps;
    size_t npath_deps;
    size_t path_dep_capacity;
    int state; /* 0 = loading, 1 = loaded */
};

RYN_DEFINE_VEC(ModuleUnitVec, ModuleUnit *)

typedef struct Loader {
    Arena *arena;
    Diagnostics *diag;
    SourceManager *sm;
    ModuleUnitVec units;
    ModuleUnit **stack;
    size_t depth;
    size_t stack_capacity;
    char **search_paths;
    size_t nsearch_paths;
    size_t search_capacity;
    bool dump_tokens;
} Loader;

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static char *canonical_or_copy(const char *path) {
    char *canonical = ryn_path_canonical(path);
    return canonical ? canonical : ryn_strdup(path);
}

static const char *unit_label(Loader *loader, const ModuleUnit *unit) {
    if (unit->alias) {
        return unit->alias;
    }
    char *base = ryn_path_basename(unit->path);
    const char *label = arena_strdup(loader->arena, base);
    free(base);
    return label;
}

/* Import aliases are file-scoped in the source language, but the merged
 * program has one global namespace. Two packages may both import their own
 * module as `util`, so declaration names get a unique internal qualifier. */
static const char *unique_qualifier(Loader *loader, const char *alias) {
    StrBuf candidate;
    sb_init(&candidate);
    sb_append(&candidate, alias);
    unsigned suffix = 1;
    for (;;) {
        bool taken = false;
        for (size_t i = 0; i < loader->units.len; i++) {
            const char *existing = loader->units.items[i]->qualifier;
            if (existing && strcmp(existing, candidate.data) == 0) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            break;
        }
        suffix++;
        sb_clear(&candidate);
        sb_appendf(&candidate, "%s.%u", alias, suffix);
    }
    const char *result = arena_strdup(loader->arena, candidate.data);
    sb_free(&candidate);
    return result;
}

static ModuleUnit *find_unit(Loader *loader, const char *canonical_path) {
    for (size_t i = 0; i < loader->units.len; i++) {
        if (strcmp(loader->units.items[i]->path, canonical_path) == 0) {
            return loader->units.items[i];
        }
    }
    return NULL;
}

static void push_stack(Loader *loader, ModuleUnit *unit) {
    if (loader->depth == loader->stack_capacity) {
        size_t capacity = loader->stack_capacity ? loader->stack_capacity * 2 : 8;
        ModuleUnit **grown =
            (ModuleUnit **)realloc(loader->stack, capacity * sizeof(ModuleUnit *));
        if (!grown) {
            fprintf(stderr, "ryn: out of memory\n");
            abort();
        }
        loader->stack = grown;
        loader->stack_capacity = capacity;
    }
    loader->stack[loader->depth++] = unit;
}

static void add_search_path(Loader *loader, const char *path) {
    if (!path || !*path) {
        return;
    }
    for (size_t i = 0; i < loader->nsearch_paths; i++) {
        if (strcmp(loader->search_paths[i], path) == 0) {
            return;
        }
    }
    if (loader->nsearch_paths == loader->search_capacity) {
        size_t capacity = loader->search_capacity ? loader->search_capacity * 2 : 8;
        char **grown =
            (char **)realloc(loader->search_paths, capacity * sizeof(char *));
        if (!grown) {
            fprintf(stderr, "ryn: out of memory\n");
            abort();
        }
        loader->search_paths = grown;
        loader->search_capacity = capacity;
    }
    loader->search_paths[loader->nsearch_paths++] = ryn_strdup(path);
}

/* Walks up from the entry file looking for the project state directory. A
 * directory with `rynx.toml` but no installed packages still counts so that
 * `ryn build` points at the same place `rynx install` would use. */
static char *find_project_packages_dir(const char *start_dir) {
    char *dir = ryn_strdup(start_dir);
    if (!dir) {
        return NULL;
    }
    for (int depth = 0; depth < 64; depth++) {
        char *packages = ryn_path_join(dir, RYNX_PACKAGES_DIR);
        if (ryn_is_directory(packages)) {
            free(dir);
            return packages;
        }
        char *manifest = ryn_path_join(dir, RYNX_MANIFEST_FILE);
        bool has_manifest = ryn_file_exists(manifest);
        free(manifest);
        if (has_manifest) {
            free(dir);
            return packages;
        }
        free(packages);
        char *parent = ryn_path_dirname(dir);
        if (strcmp(parent, dir) == 0) {
            free(parent);
            break;
        }
        free(dir);
        dir = parent;
    }
    free(dir);
    return NULL;
}

static void add_environment_search_paths(Loader *loader) {
    char *value = ryn_getenv("RYN_MODULE_PATH");
    if (!value) {
        return;
    }
#if defined(_WIN32)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    char *start = value;
    for (char *cursor = value;; cursor++) {
        if (*cursor == separator || *cursor == '\0') {
            char saved = *cursor;
            *cursor = '\0';
            if (*start) {
                add_search_path(loader, start);
            }
            *cursor = saved;
            if (saved == '\0') {
                break;
            }
            start = cursor + 1;
        }
    }
    free(value);
}

/* ------------------------------------------------------------------ */
/* Module lookup                                                      */
/* ------------------------------------------------------------------ */

static char *try_relative_file(const char *dir, const char *relative) {
    char *path = ryn_path_join(dir, relative);
    if (ryn_file_exists(path)) {
        return path;
    }
    free(path);
    return NULL;
}

static char *try_manifest_entry(const char *package_dir) {
    char *manifest_path = ryn_path_join(package_dir, RYNX_MANIFEST_FILE);
    if (!ryn_file_exists(manifest_path)) {
        free(manifest_path);
        return NULL;
    }
    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[512] = {0};
    bool parsed = rynx_manifest_load(manifest_path, &arena, &manifest, err, sizeof(err));
    free(manifest_path);
    char *result = NULL;
    if (parsed && manifest.entry) {
        result = try_relative_file(package_dir, manifest.entry);
    }
    rynx_manifest_free(&manifest);
    arena_free(&arena);
    return result;
}

static char *try_package_directory(const char *package, const char *name) {
    if (!ryn_is_directory(package)) {
        return NULL;
    }
    StrBuf relative;
    sb_init(&relative);
    sb_appendf(&relative, "src/lib.ryn");
    char *found = try_relative_file(package, relative.data);
    if (found) {
        sb_free(&relative);
        return found;
    }
    sb_clear(&relative);
    sb_appendf(&relative, "src/%s.ryn", name);
    found = try_relative_file(package, relative.data);
    if (found) {
        sb_free(&relative);
        return found;
    }
    sb_clear(&relative);
    sb_append(&relative, "src/mod.ryn");
    found = try_relative_file(package, relative.data);
    if (found) {
        sb_free(&relative);
        return found;
    }
    sb_free(&relative);

    sb_init(&relative);
    sb_appendf(&relative, "%s.ryn", name);
    found = try_relative_file(package, relative.data);
    if (found) {
        sb_free(&relative);
        return found;
    }
    sb_clear(&relative);
    sb_append(&relative, "mod.ryn");
    found = try_relative_file(package, relative.data);
    if (found) {
        sb_free(&relative);
        return found;
    }
    sb_clear(&relative);
    sb_append(&relative, "lib.ryn");
    found = try_relative_file(package, relative.data);
    sb_free(&relative);
    if (!found) {
        found = try_manifest_entry(package);
    }
    return found;
}

static char *try_module_dir(const char *base, const char *name) {
    if (!ryn_is_directory(base)) {
        return NULL;
    }
    char *package = ryn_path_join(base, name);
    char *found = try_package_directory(package, name);
    free(package);
    return found;
}

/* Reads `path` dependencies from the manifest closest to a source file. A
 * module inside a package inherits that package's dependencies. Failures are
 * ignored: unresolved imports are reported when the import is processed. */
static void load_unit_path_dependencies(Loader *loader, ModuleUnit *unit) {
    char *dir = ryn_strdup(unit->dir);
    if (!dir) {
        return;
    }
    char *manifest_dir = NULL;
    char *manifest_path = NULL;
    for (int depth = 0; depth < 64; depth++) {
        char *candidate = ryn_path_join(dir, RYNX_MANIFEST_FILE);
        if (ryn_file_exists(candidate)) {
            manifest_path = candidate;
            manifest_dir = ryn_strdup(dir);
            free(dir);
            dir = NULL;
            break;
        }
        free(candidate);
        char *parent = ryn_path_dirname(dir);
        if (strcmp(parent, dir) == 0) {
            free(parent);
            break;
        }
        free(dir);
        dir = parent;
    }
    free(dir);
    if (!manifest_path) {
        return;
    }

    Arena arena;
    arena_init(&arena);
    RynxManifest manifest;
    rynx_manifest_init(&manifest);
    char err[512] = {0};
    if (rynx_manifest_load(manifest_path, &arena, &manifest, err, sizeof(err))) {
        for (size_t i = 0; i < manifest.deps.len; i++) {
            RynxDep *dep = &manifest.deps.items[i];
            if (dep->kind != RYNX_DEP_PATH || !dep->name || !dep->path) {
                continue;
            }
            char *resolved = ryn_path_join(manifest_dir, dep->path);
            char *canonical = canonical_or_copy(resolved);
            free(resolved);
            if (!canonical) {
                continue;
            }
            if (unit->npath_deps == unit->path_dep_capacity) {
                size_t capacity = unit->path_dep_capacity ? unit->path_dep_capacity * 2 : 4;
                PathDependency *grown = (PathDependency *)realloc(
                    unit->path_deps, capacity * sizeof(PathDependency));
                if (!grown) {
                    fprintf(stderr, "ryn: out of memory\n");
                    abort();
                }
                unit->path_deps = grown;
                unit->path_dep_capacity = capacity;
            }
            unit->path_deps[unit->npath_deps].name =
                arena_strdup(loader->arena, dep->name);
            unit->path_deps[unit->npath_deps].dir =
                arena_strdup(loader->arena, canonical);
            unit->npath_deps++;
            free(canonical);
        }
    }
    rynx_manifest_free(&manifest);
    arena_free(&arena);
    free(manifest_path);
    free(manifest_dir);
}

static char *resolve_import_file(Loader *loader, const ModuleUnit *importer,
                                 const char *name) {
    StrBuf relative;
    sb_init(&relative);
    sb_appendf(&relative, "%s.ryn", name);
    char *found = try_relative_file(importer->dir, relative.data);
    if (found) {
        sb_free(&relative);
        return found;
    }
    sb_clear(&relative);
    sb_appendf(&relative, "%s/mod.ryn", name);
    found = try_relative_file(importer->dir, relative.data);
    sb_free(&relative);
    if (found) {
        return found;
    }

    for (size_t i = 0; i < importer->npath_deps; i++) {
        if (strcmp(importer->path_deps[i].name, name) == 0) {
            found = try_package_directory(importer->path_deps[i].dir, name);
            if (found) {
                return found;
            }
        }
    }

    for (size_t i = 0; i < loader->nsearch_paths; i++) {
        const char *base = loader->search_paths[i];
        StrBuf flat;
        sb_init(&flat);
        sb_appendf(&flat, "%s.ryn", name);
        found = try_relative_file(base, flat.data);
        sb_free(&flat);
        if (found) {
            return found;
        }
        StrBuf mod;
        sb_init(&mod);
        sb_appendf(&mod, "%s/mod.ryn", name);
        found = try_relative_file(base, mod.data);
        sb_free(&mod);
        if (found) {
            return found;
        }
        found = try_module_dir(base, name);
        if (found) {
            return found;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Loading and parsing                                                */
/* ------------------------------------------------------------------ */

static AstProgram *parse_source(Loader *loader, ModuleUnit *unit) {
    Lexer lexer;
    lexer_init(&lexer, loader->arena, loader->diag, loader->sm);
    bool lexed = lexer_lex(&lexer, unit->file);
    if (loader->dump_tokens) {
        printf("== %s ==\n", unit->path);
        lexer_dump_tokens(&lexer.tokens, stdout);
    }
    AstProgram *program = NULL;
    if (lexed) {
        Parser parser;
        parser_init(&parser, loader->arena, loader->diag, loader->sm, &lexer.tokens);
        program = parser_parse_program(&parser, unit->file);
    }
    lexer_free(&lexer);
    return program;
}

static void report_cycle(Loader *loader, ModuleUnit *target, SourcePos pos) {
    size_t start = 0;
    for (size_t i = 0; i < loader->depth; i++) {
        if (loader->stack[i] == target) {
            start = i;
            break;
        }
    }
    StrBuf chain;
    sb_init(&chain);
    for (size_t i = start; i < loader->depth; i++) {
        if (i > start) {
            sb_append(&chain, " -> ");
        }
        sb_append(&chain, unit_label(loader, loader->stack[i]));
    }
    const char *label = unit_label(loader, target);
    sb_append(&chain, " -> ");
    sb_append(&chain, label);
    diag_error_note(loader->diag, pos, (int)strlen(label), E_IMPORT_CYCLE, chain.data,
                    "import cycle detected while loading '%s'", label);
    sb_free(&chain);
}

static ModuleUnit *load_unit(Loader *loader, const char *path, const char *alias,
                             SourcePos pos, SourceFile *preloaded) {
    char *canonical = canonical_or_copy(path);
    ModuleUnit *existing = find_unit(loader, canonical);
    if (existing) {
        if (existing->state == 0) {
            report_cycle(loader, existing, pos);
            free(canonical);
            return NULL;
        }
        if (alias && existing->alias && strcmp(alias, existing->alias) != 0) {
            diag_error_note(loader->diag, pos, (int)strlen(alias), E_DUPLICATE,
                            "a module file can only be imported under one name per build",
                            "module file '%s' is already imported as '%s'", canonical,
                            existing->alias);
            free(canonical);
            return NULL;
        }
        free(canonical);
        return existing;
    }

    SourceFile *file = preloaded;
    if (!file) {
        char err[512] = {0};
        file = sm_load_file(loader->sm, canonical, err, sizeof(err));
        if (!file) {
            diag_error(loader->diag, pos, E_IO, "cannot read module '%s': %s", canonical,
                       err);
            free(canonical);
            return NULL;
        }
    }

    ModuleUnit *unit = (ModuleUnit *)calloc(1, sizeof(ModuleUnit));
    if (!unit) {
        fprintf(stderr, "ryn: out of memory\n");
        abort();
    }
    unit->path = arena_strdup(loader->arena, canonical);
    unit->dir = ryn_path_dirname(canonical);
    unit->alias = alias ? arena_strdup(loader->arena, alias) : NULL;
    unit->qualifier = alias ? unique_qualifier(loader, alias) : NULL;
    unit->file = file;
    unit->state = 0;
    free(canonical);
    ModuleUnitVec_push(&loader->units, unit);
    load_unit_path_dependencies(loader, unit);
    push_stack(loader, unit);

    unit->ast = parse_source(loader, unit);
    if (!unit->ast) {
        unit->state = 1;
        loader->depth--;
        return NULL;
    }

    for (size_t i = 0; i < unit->ast->decls.len; i++) {
        AstDecl *decl = unit->ast->decls.items[i];
        if (decl->kind != AST_IMPORT_DECL) {
            continue;
        }
        AstImportDecl *import = (AstImportDecl *)decl;
        bool duplicate = false;
        for (size_t k = 0; k < unit->nimports; k++) {
            if (strcmp(unit->imports[k].alias, import->module) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            diag_error(loader->diag, import->base.pos, E_DUPLICATE,
                       "module '%s' is already imported in this file", import->module);
            continue;
        }

        ModuleImport record;
        memset(&record, 0, sizeof(record));
        record.alias = arena_strdup(loader->arena, import->module);
        record.pos = import->base.pos;

        if (stdlib_find_module(import->module)) {
            record.kind = MODULE_IMPORT_BUILTIN;
        } else {
            char *found = resolve_import_file(loader, unit, import->module);
            if (!found) {
                record.kind = MODULE_IMPORT_UNRESOLVED;
                diag_error_note(
                    loader->diag, import->base.pos, (int)strlen(import->module),
                    E_UNKNOWN_MODULE,
                    "searched next to the importer, in path dependencies from rynx.toml, "
                    "in --module-path and $RYN_MODULE_PATH directories, and in "
                    ".rynx/packages",
                    "unknown module '%s'", import->module);
            } else {
                ModuleUnit *child = load_unit(loader, found, import->module, import->base.pos,
                                              NULL);
                free(found);
                record.kind = child ? MODULE_IMPORT_USER : MODULE_IMPORT_UNRESOLVED;
                record.unit = child;
            }
        }

        if (unit->nimports == unit->import_capacity) {
            size_t capacity = unit->import_capacity ? unit->import_capacity * 2 : 8;
            ModuleImport *grown = (ModuleImport *)realloc(
                unit->imports, capacity * sizeof(ModuleImport));
            if (!grown) {
                fprintf(stderr, "ryn: out of memory\n");
                abort();
            }
            unit->imports = grown;
            unit->import_capacity = capacity;
        }
        unit->imports[unit->nimports++] = record;
    }

    unit->state = 1;
    loader->depth--;
    return unit;
}

/* ------------------------------------------------------------------ */
/* Renaming and reference rewriting                                   */
/* ------------------------------------------------------------------ */

typedef struct NameSet {
    const char **items;
    size_t len;
    size_t capacity;
} NameSet;

static void name_set_add(NameSet *set, const char *name) {
    if (!name || !*name) {
        return;
    }
    for (size_t i = 0; i < set->len; i++) {
        if (strcmp(set->items[i], name) == 0) {
            return;
        }
    }
    if (set->len == set->capacity) {
        size_t capacity = set->capacity ? set->capacity * 2 : 8;
        const char **grown =
            (const char **)realloc(set->items, capacity * sizeof(const char *));
        if (!grown) {
            fprintf(stderr, "ryn: out of memory\n");
            abort();
        }
        set->items = grown;
        set->capacity = capacity;
    }
    set->items[set->len++] = name;
}

static bool name_set_has(const NameSet *set, const char *name) {
    for (size_t i = 0; i < set->len; i++) {
        if (strcmp(set->items[i], name) == 0) {
            return true;
        }
    }
    return false;
}

typedef struct LocalScope {
    NameSet names;
    struct LocalScope *parent;
} LocalScope;

typedef struct Rewrite {
    Arena *arena;
    ModuleUnit *unit;
    NameSet functions;
    NameSet types;
    LocalScope *scope;
} Rewrite;

static bool locals_have(const Rewrite *rw, const char *name) {
    for (const LocalScope *scope = rw->scope; scope; scope = scope->parent) {
        if (name_set_has(&scope->names, name)) {
            return true;
        }
    }
    return false;
}

static ModuleUnit *unit_user_import(const ModuleUnit *unit, const char *alias) {
    for (size_t i = 0; i < unit->nimports; i++) {
        if (unit->imports[i].kind == MODULE_IMPORT_USER &&
            strcmp(unit->imports[i].alias, alias) == 0) {
            return unit->imports[i].unit;
        }
    }
    return NULL;
}

static const char *qualify(Rewrite *rw, const char *name) {
    if (!rw->unit->qualifier) {
        return name;
    }
    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "%s.%s", rw->unit->qualifier, name);
    const char *result = arena_strdup(rw->arena, sb.data);
    sb_free(&sb);
    return result;
}

static void rewrite_type(Rewrite *rw, AstType *type);
static void rewrite_expr(Rewrite *rw, AstExpr **slot);
static void rewrite_stmt(Rewrite *rw, AstStmt *stmt);
static void rewrite_block(Rewrite *rw, AstBlock *block);

static void rewrite_type(Rewrite *rw, AstType *type) {
    if (!type) {
        return;
    }
    switch (type->kind) {
    case AST_TYPE_NAME:
        if (rw->unit->qualifier && name_set_has(&rw->types, type->name)) {
            type->name = qualify(rw, type->name);
        }
        break;
    case AST_TYPE_PTR:
        rewrite_type(rw, type->elem);
        break;
    case AST_TYPE_ARRAY:
        rewrite_type(rw, type->elem);
        break;
    case AST_TYPE_FN:
        for (size_t i = 0; i < type->nparams; i++) {
            rewrite_type(rw, type->params[i]);
        }
        rewrite_type(rw, type->ret);
        break;
    default:
        break;
    }
}

static void rewrite_expr(Rewrite *rw, AstExpr **slot) {
    if (!slot || !*slot) {
        return;
    }
    AstExpr *expr = *slot;
    switch (expr->kind) {
    case AST_IDENT: {
        AstIdent *ident = (AstIdent *)expr;
        if (rw->unit->qualifier && !locals_have(rw, ident->name) &&
            (name_set_has(&rw->functions, ident->name) ||
             name_set_has(&rw->types, ident->name))) {
            ident->name = qualify(rw, ident->name);
        }
        break;
    }
    case AST_UNARY:
        rewrite_expr(rw, &((AstUnary *)expr)->operand);
        break;
    case AST_BINARY: {
        AstBinary *binary = (AstBinary *)expr;
        rewrite_expr(rw, &binary->lhs);
        rewrite_expr(rw, &binary->rhs);
        break;
    }
    case AST_CALL: {
        AstCall *call = (AstCall *)expr;
        rewrite_expr(rw, &call->callee);
        for (size_t i = 0; i < call->argc; i++) {
            rewrite_expr(rw, &call->args[i]);
        }
        break;
    }
    case AST_FIELD: {
        AstField *field = (AstField *)expr;
        rewrite_expr(rw, &field->object);
        if (field->object->kind == AST_IDENT) {
            AstIdent *object = (AstIdent *)field->object;
            ModuleUnit *imported =
                locals_have(rw, object->name) ? NULL
                                              : unit_user_import(rw->unit, object->name);
            if (imported && imported->qualifier) {
                StrBuf sb;
                sb_init(&sb);
                sb_appendf(&sb, "%s.%s", imported->qualifier, field->name);
                AstExpr *replacement =
                    ast_ident(rw->arena, field->base.pos, arena_strdup(rw->arena, sb.data));
                sb_free(&sb);
                *slot = replacement;
            }
        }
        break;
    }
    case AST_INDEX: {
        AstIndex *index = (AstIndex *)expr;
        rewrite_expr(rw, &index->object);
        rewrite_expr(rw, &index->index);
        break;
    }
    case AST_CAST: {
        AstCast *cast = (AstCast *)expr;
        rewrite_expr(rw, &cast->operand);
        rewrite_type(rw, cast->target_type);
        break;
    }
    case AST_STRUCT_LITERAL: {
        AstStructLiteral *literal = (AstStructLiteral *)expr;
        rewrite_type(rw, literal->type_syntax);
        for (size_t i = 0; i < literal->count; i++) {
            rewrite_expr(rw, &literal->field_values[i]);
        }
        break;
    }
    case AST_ARRAY_LITERAL: {
        AstArrayLiteral *literal = (AstArrayLiteral *)expr;
        for (size_t i = 0; i < literal->count; i++) {
            rewrite_expr(rw, &literal->elements[i]);
        }
        break;
    }
    default:
        break;
    }
}

static void rewrite_block(Rewrite *rw, AstBlock *block) {
    if (!block) {
        return;
    }
    LocalScope scope;
    memset(&scope, 0, sizeof(scope));
    scope.parent = rw->scope;
    rw->scope = &scope;
    for (size_t i = 0; i < block->count; i++) {
        rewrite_stmt(rw, block->stmts[i]);
    }
    rw->scope = scope.parent;
    free(scope.names.items);
}

static void rewrite_stmt(Rewrite *rw, AstStmt *stmt) {
    if (!stmt) {
        return;
    }
    switch (stmt->kind) {
    case AST_VAR_DECL: {
        AstVarDecl *decl = (AstVarDecl *)stmt;
        rewrite_type(rw, decl->type_syntax);
        rewrite_expr(rw, &decl->init);
        if (rw->scope) {
            name_set_add(&rw->scope->names, decl->name);
        }
        break;
    }
    case AST_ASSIGN: {
        AstAssign *assign = (AstAssign *)stmt;
        rewrite_expr(rw, &assign->target);
        rewrite_expr(rw, &assign->value);
        break;
    }
    case AST_EXPR_STMT:
        rewrite_expr(rw, &((AstExprStmt *)stmt)->expr);
        break;
    case AST_IF: {
        AstIf *if_stmt = (AstIf *)stmt;
        rewrite_expr(rw, &if_stmt->cond);
        rewrite_block(rw, if_stmt->then_block);
        if (if_stmt->else_branch) {
            rewrite_stmt(rw, if_stmt->else_branch);
        }
        break;
    }
    case AST_WHILE: {
        AstWhile *while_stmt = (AstWhile *)stmt;
        rewrite_expr(rw, &while_stmt->cond);
        rewrite_block(rw, while_stmt->body);
        break;
    }
    case AST_FOR: {
        AstFor *for_stmt = (AstFor *)stmt;
        rewrite_expr(rw, &for_stmt->start);
        rewrite_expr(rw, &for_stmt->end);
        LocalScope scope;
        memset(&scope, 0, sizeof(scope));
        scope.parent = rw->scope;
        rw->scope = &scope;
        name_set_add(&scope.names, for_stmt->var);
        rewrite_block(rw, for_stmt->body);
        rw->scope = scope.parent;
        free(scope.names.items);
        break;
    }
    case AST_RETURN:
        rewrite_expr(rw, &((AstReturn *)stmt)->value);
        break;
    case AST_BLOCK_STMT:
        rewrite_block(rw, ((AstBlockStmt *)stmt)->block);
        break;
    default:
        break;
    }
}

static void rewrite_function(Rewrite *rw, AstFnDecl *fn) {
    LocalScope scope;
    memset(&scope, 0, sizeof(scope));
    scope.parent = rw->scope;
    rw->scope = &scope;
    for (size_t i = 0; i < fn->nparams; i++) {
        rewrite_type(rw, fn->params[i].type_syntax);
        name_set_add(&scope.names, fn->params[i].name);
    }
    rewrite_type(rw, fn->return_type_syntax);
    rewrite_block(rw, fn->body);
    rw->scope = scope.parent;
    free(scope.names.items);
}

static void collect_names(ModuleUnit *unit, NameSet *functions, NameSet *types) {
    for (size_t i = 0; i < unit->ast->decls.len; i++) {
        AstDecl *decl = unit->ast->decls.items[i];
        if (decl->kind == AST_FN_DECL) {
            name_set_add(functions, ((AstFnDecl *)decl)->name);
        } else if (decl->kind == AST_STRUCT_DECL) {
            name_set_add(types, ((AstStructDecl *)decl)->name);
        } else if (decl->kind == AST_ENUM_DECL) {
            name_set_add(types, ((AstEnumDecl *)decl)->name);
        }
    }
}

static void rewrite_unit(ModuleUnit *unit) {
    if (!unit->ast) {
        return;
    }
    Rewrite rw;
    memset(&rw, 0, sizeof(rw));
    rw.arena = unit->ast->arena;
    rw.unit = unit;
    collect_names(unit, &rw.functions, &rw.types);

    for (size_t i = 0; i < unit->ast->decls.len; i++) {
        AstDecl *decl = unit->ast->decls.items[i];
        switch (decl->kind) {
        case AST_FN_DECL: {
            AstFnDecl *fn = (AstFnDecl *)decl;
            if (unit->alias) {
                fn->name = qualify(&rw, fn->name);
            }
            rewrite_function(&rw, fn);
            break;
        }
        case AST_STRUCT_DECL: {
            AstStructDecl *st = (AstStructDecl *)decl;
            if (unit->alias) {
                st->name = qualify(&rw, st->name);
            }
            for (size_t f = 0; f < st->nfields; f++) {
                rewrite_type(&rw, st->fields[f].type_syntax);
            }
            break;
        }
        case AST_ENUM_DECL: {
            AstEnumDecl *en = (AstEnumDecl *)decl;
            if (unit->alias) {
                en->name = qualify(&rw, en->name);
            }
            break;
        }
        default:
            break;
        }
    }

    free(rw.functions.items);
    free(rw.types.items);
}

/* ------------------------------------------------------------------ */
/* Merging                                                            */
/* ------------------------------------------------------------------ */

static bool unit_import_is_builtin(const ModuleUnit *unit, const char *alias) {
    for (size_t i = 0; i < unit->nimports; i++) {
        if (strcmp(unit->imports[i].alias, alias) == 0) {
            return unit->imports[i].kind == MODULE_IMPORT_BUILTIN;
        }
    }
    return false;
}

static AstProgram *merge_units(Loader *loader) {
    AstProgram *merged = ast_program_new(loader->arena);
    NameSet builtin_imports;
    memset(&builtin_imports, 0, sizeof(builtin_imports));
    for (size_t u = 0; u < loader->units.len; u++) {
        ModuleUnit *unit = loader->units.items[u];
        if (!unit->ast) {
            continue;
        }
        for (size_t i = 0; i < unit->ast->decls.len; i++) {
            AstDecl *decl = unit->ast->decls.items[i];
            if (decl->kind == AST_IMPORT_DECL) {
                AstImportDecl *import = (AstImportDecl *)decl;
                if (!unit_import_is_builtin(unit, import->module)) {
                    /* User modules are merged into the program directly. */
                    continue;
                }
                if (name_set_has(&builtin_imports, import->module)) {
                    continue;
                }
                name_set_add(&builtin_imports, import->module);
            }
            ast_program_add(merged, decl);
        }
    }
    free(builtin_imports.items);
    return merged;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

bool modules_load_program(Arena *arena, Diagnostics *diag, SourceManager *sm,
                          const char *entry_path, const ModuleOptions *options,
                          bool dump_tokens, AstProgram **out_program) {
    Loader loader;
    memset(&loader, 0, sizeof(loader));
    loader.arena = arena;
    loader.diag = diag;
    loader.sm = sm;
    loader.dump_tokens = dump_tokens;
    ModuleUnitVec_init(&loader.units);

    if (options) {
        for (size_t i = 0; i < options->nsearch_paths; i++) {
            add_search_path(&loader, options->search_paths[i]);
        }
    }
    add_environment_search_paths(&loader);

    char *canonical_entry = canonical_or_copy(entry_path);
    char *entry_dir = ryn_path_dirname(canonical_entry);
    char *packages = find_project_packages_dir(entry_dir);
    if (packages) {
        add_search_path(&loader, packages);
        free(packages);
    }
    free(entry_dir);

    char err[512] = {0};
    SourceFile *root_file = sm_load_file(sm, entry_path, err, sizeof(err));
    if (!root_file) {
        diag_error(diag, (SourcePos){-1, 1, 1, 0}, E_IO, "cannot read '%s': %s", entry_path,
                   err);
        free(canonical_entry);
        ModuleUnitVec_free(&loader.units);
        for (size_t i = 0; i < loader.nsearch_paths; i++) {
            free(loader.search_paths[i]);
        }
        free(loader.search_paths);
        free(loader.stack);
        return false;
    }

    SourcePos root_pos = {0, 1, 1, 0};
    ModuleUnit *root = load_unit(&loader, canonical_entry, NULL, root_pos, root_file);
    free(canonical_entry);

    bool ok = root != NULL && root->ast != NULL && diag->error_count == 0;
    if (ok) {
        for (size_t i = 0; i < loader.units.len; i++) {
            rewrite_unit(loader.units.items[i]);
        }
        *out_program = merge_units(&loader);
    }

    for (size_t i = 0; i < loader.units.len; i++) {
        ModuleUnit *unit = loader.units.items[i];
        ast_program_free(unit->ast);
        free(unit->imports);
        free(unit->path_deps);
        free((char *)unit->dir);
        free(unit);
    }
    ModuleUnitVec_free(&loader.units);
    for (size_t i = 0; i < loader.nsearch_paths; i++) {
        free(loader.search_paths[i]);
    }
    free(loader.search_paths);
    free(loader.stack);
    return ok;
}
