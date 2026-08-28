/* pymergetic.metal.build — __pmm__.toml manifests as data.
 *
 * (a) a minimal TOML reader: exactly the Phase-1 schema
 *     (key = "string" | integer | ["a", "b"], # comments, [table] headers
 *     accepted and skipped — the manifests are flat),
 * (b) unit_parse turning manifest bytes into a pm_metal_build_unit_t,
 * (c) graph_resolve: topological order on depends (cycle = error),
 * (d) compile_source / link: Phase 3 fills these (TCC objects into the
 *     in-tree ELF relocator).
 */
#include "pymergetic/metal/build/__exports__.h"

#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/util/mem.h"

#include "pymergetic/metal/inspect/src_embed.inc.h"

#include <stdio.h>
#include <string.h>

/*------------------ build records (provenance chain) ------------------
 * Retained per unit_compile. Arena memory dies with the caller's arena, but
 * the record must outlive it (the inspector serves it later), so names and
 * lengths are copied into fixed static storage. Object bytes stay arena-
 * owned and are NOT retained — the record carries lengths + symbols only;
 * the inspector's /build/<fqn>/<file> pane serves authored source with
 * provenance, not a byte dump of the .o. */
static pm_metal_build_record_t s_records[PM_METAL_BUILD_MAX_RECORDS];
static uint32_t s_record_epoch;

/* Symbol names live inside each record — a refresh-in-place rebuild simply
 * overwrites them, so no global pool exhaustion and no cross-record aliasing. */
typedef struct pm_build_rec_sym_ctx {
    pm_metal_build_record_t *r;
    uint32_t w;
} pm_build_rec_sym_ctx_t;

#ifdef PM_METAL_BUILD_HAS_ELF
static void record_sym_cb(const char *name, void *addr, void *ctx_in) {
    pm_build_rec_sym_ctx_t *ctx = (pm_build_rec_sym_ctx_t *)ctx_in;
    (void)addr;
    if (ctx == NULL || ctx->r == NULL
        || ctx->w >= PM_METAL_BUILD_MAX_SYMS) {
        return;
    }
    snprintf(ctx->r->sym_names_buf[ctx->w], PM_METAL_BUILD_SYM_NAME_MAX, "%s", name);
    ctx->r->sym_names[ctx->w] = ctx->r->sym_names_buf[ctx->w];
    ctx->w++;
}
#endif

static pm_metal_build_record_t *record_slot(const char *fqn) {
    uint32_t i;
    for (i = 0; i < PM_METAL_BUILD_MAX_RECORDS; i++) {
        if (s_records[i].valid && strcmp(s_records[i].fqn, fqn) == 0) {
            return &s_records[i];
        }
    }
    return NULL;
}

static pm_metal_build_record_t *record_slot_acquire(const char *fqn) {
    pm_metal_build_record_t *r = record_slot(fqn);
    if (r != NULL) {
        return r;   /* rebuild of an already-recorded unit: refresh in place */
    }
    /* oldest-slot eviction: epoch round-robins through the table */
    r = &s_records[s_record_epoch % PM_METAL_BUILD_MAX_RECORDS];
    s_record_epoch++;
    memset(r, 0, sizeof(*r));
    snprintf(r->fqn, sizeof(r->fqn), "%s", fqn);
    r->valid = 1;
    return r;
}

const pm_metal_build_record_t *pm_metal_build_record_find(const char *fqn) {
    if (fqn == NULL) {
        return NULL;
    }
    return record_slot(fqn);
}

void pm_metal_build_record_reset(void) {
    memset(s_records, 0, sizeof(s_records));
    s_record_epoch = 0;
}

/*------------------ error helpers ------------------*/

static void err_set(char *errbuf, size_t errbuf_len, const char *fmt, int line) {
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    snprintf(errbuf, errbuf_len, "line %d: %s", line, fmt);
}

/*------------------ lexer ------------------*/

typedef struct pm_build_lex {
    const char *p;
    const char *end;
    int line;
} pm_build_lex_t;

static char lex_peek(pm_build_lex_t *lx) {
    return lx->p < lx->end ? *lx->p : '\0';
}

static void lex_skip_ws(pm_build_lex_t *lx) {
    for (;;) {
        char c = lex_peek(lx);
        if (c == ' ' || c == '\t' || c == '\r') {
            lx->p++;
        } else if (c == '\n') {
            lx->p++;
            lx->line++;
        } else if (c == '#') {
            while (lx->p < lx->end && *lx->p != '\n') {
                lx->p++;
            }
        } else {
            return;
        }
    }
}

/*------------------ unit assembly ------------------*/

typedef struct pm_build_strings {
    const char **items;
    uint32_t n;
    uint32_t cap;
    int oom;
} pm_build_strings_t;

static void strings_push(pm_util_mem_arena_t *arena, pm_build_strings_t *s, const char *v) {
    if (s->oom) {
        return;
    }
    if (s->n == s->cap) {
        uint32_t ncap = s->cap == 0 ? 8u : s->cap * 2u;
        const char **ng = (const char **)pm_util_mem_alloc(arena, ncap * sizeof(const char *));
        if (ng == NULL) {
            s->oom = 1;
            return;
        }
        if (s->n > 0) {
            memcpy(ng, s->items, s->n * sizeof(const char *));
        }
        s->items = ng;
        s->cap = ncap;
    }
    s->items[s->n++] = v;
}

static const char *dup_str(pm_util_mem_arena_t *arena, const char *src, size_t len) {
    char *d = (char *)pm_util_mem_alloc(arena, len + 1u);
    if (d == NULL) {
        return NULL;
    }
    memcpy(d, src, len);
    d[len] = '\0';
    return d;
}

/* Read a double-quoted string (caller consumed the opening quote). Escapes
 * limited to \" and \\ — the schema never needs more. */
static const char *parse_string(pm_util_mem_arena_t *arena, pm_build_lex_t *lx,
    int *bad_line) {
    const char *start = lx->p;
    size_t len = 0;
    while (lx->p < lx->end && *lx->p != '"') {
        if (*lx->p == '\n') {
            *bad_line = lx->line;
            return NULL;
        }
        if (*lx->p == '\\' && lx->p + 1 < lx->end
            && (lx->p[1] == '"' || lx->p[1] == '\\')) {
            lx->p++;
        }
        lx->p++;
        len++;
    }
    if (lx->p >= lx->end) {
        *bad_line = lx->line;
        return NULL;
    }
    lx->p++;  /* closing quote */
    char *out = (char *)pm_util_mem_alloc(arena, len + 1u);
    if (out == NULL) {
        return NULL;
    }
    const char *r = start;
    size_t w = 0;
    while (r < lx->p - 1) {
        if (*r == '\\' && r + 1 < lx->p - 1 && (r[1] == '"' || r[1] == '\\')) {
            r++;
        }
        out[w++] = *r++;
    }
    out[w] = '\0';
    return out;
}

/* Append a bare key token (up to '=' ). Returns arena-owned copy. */
static const char *parse_key(pm_util_mem_arena_t *arena, pm_build_lex_t *lx, int *bad_line) {
    const char *start = lx->p;
    while (lx->p < lx->end) {
        char c = *lx->p;
        if (c == '=' || c == '\n' || c == '#' || c == ' ' || c == '\t' || c == '\r') {
            break;
        }
        lx->p++;
    }
    size_t len = (size_t)(lx->p - start);
    if (len == 0) {
        *bad_line = lx->line;
        return NULL;
    }
    return dup_str(arena, start, len);
}

static int expect(pm_build_lex_t *lx, char c, int *bad_line) {
    if (lx->p < lx->end && *lx->p == c) {
        lx->p++;
        return 1;
    }
    *bad_line = lx->line;
    return 0;
}

/*------------------ TOML -> unit ------------------*/

static int set_scalar(pm_util_mem_arena_t *arena, pm_metal_build_unit_t *u,
    const char *key, const char *val, int *oom) {
    if (strcmp(key, "fqn") == 0) {
        if (strlen(val) >= sizeof(u->fqn)) return -1;
        snprintf(u->fqn, sizeof(u->fqn), "%s", val);
    } else if (strcmp(key, "impl") == 0) {
        if (strlen(val) >= sizeof(u->impl)) return -1;
        snprintf(u->impl, sizeof(u->impl), "%s", val);
    } else if (strcmp(key, "version") == 0) {
        if (strlen(val) >= sizeof(u->version)) return -1;
        snprintf(u->version, sizeof(u->version), "%s", val);
    } else if (strcmp(key, "upstream") == 0 || strcmp(key, "archive") == 0
        || strcmp(key, "notes") == 0) {
        /* provenance — carried by the manifest file, not by the unit */
    } else {
        return 0;
    }
    (void)arena;
    (void)oom;
    return 1;
}

int32_t pm_metal_build_unit_parse(pm_util_mem_arena_t *arena,
    const uint8_t *bytes, size_t len, pm_metal_build_unit_t *unit,
    char *errbuf, size_t errbuf_len) {
    pm_build_lex_t lx = { (const char *)bytes, (const char *)bytes + len, 1 };
    pm_build_strings_t sources = { 0 }, includes = { 0 }, defines = { 0 }, depends = { 0 };
    pm_build_strings_t *dst = NULL;
    int bad = 0;

    if (arena == NULL || bytes == NULL || unit == NULL) {
        err_set(errbuf, errbuf_len, "null argument", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    memset(unit, 0, sizeof(*unit));

    for (;;) {
        lex_skip_ws(&lx);
        if (lx.p >= lx.end) {
            break;
        }
        if (*lx.p == '[') {
            /* [table] header — the manifests are flat, skip the line */
            while (lx.p < lx.end && *lx.p != '\n') {
                lx.p++;
            }
            continue;
        }
        const char *key = parse_key(arena, &lx, &bad);
        if (key == NULL) {
            err_set(errbuf, errbuf_len, "expected key", bad ? bad : lx.line);
            return PM_METAL_BUILD_ERR_PARSE;
        }
        lex_skip_ws(&lx);
        if (!expect(&lx, '=', &bad)) {
            err_set(errbuf, errbuf_len, "expected '=' after key", lx.line);
            return PM_METAL_BUILD_ERR_PARSE;
        }
        lex_skip_ws(&lx);

        if (lex_peek(&lx) == '"') {
            lx.p++;
            const char *val = parse_string(arena, &lx, &bad);
            if (val == NULL) {
                err_set(errbuf, errbuf_len, "unterminated string", bad ? bad : lx.line);
                return PM_METAL_BUILD_ERR_PARSE;
            }
            int rc = set_scalar(arena, unit, key, val, &bad);
            if (rc < 0) {
                err_set(errbuf, errbuf_len, "value too long", lx.line);
                return PM_METAL_BUILD_ERR_PARSE;
            }
        } else if (lex_peek(&lx) == '[') {
            lx.p++;
            dst = NULL;
            if (strcmp(key, "sources") == 0) {
                dst = &sources;
            } else if (strcmp(key, "include_dirs") == 0) {
                dst = &includes;
            } else if (strcmp(key, "defines") == 0) {
                dst = &defines;
            } else if (strcmp(key, "depends") == 0) {
                dst = &depends;
            } else {
                err_set(errbuf, errbuf_len, "unknown array key", lx.line);
                return PM_METAL_BUILD_ERR_PARSE;
            }
            for (;;) {
                lex_skip_ws(&lx);
                if (lex_peek(&lx) == ']') {
                    lx.p++;
                    break;
                }
                if (lx.p >= lx.end) {
                    err_set(errbuf, errbuf_len, "unterminated array", lx.line);
                    return PM_METAL_BUILD_ERR_PARSE;
                }
                if (!expect(&lx, '"', &bad)) {
                    err_set(errbuf, errbuf_len, "expected string in array", lx.line);
                    return PM_METAL_BUILD_ERR_PARSE;
                }
                const char *val = parse_string(arena, &lx, &bad);
                if (val == NULL) {
                    err_set(errbuf, errbuf_len, "unterminated string in array",
                        bad ? bad : lx.line);
                    return PM_METAL_BUILD_ERR_PARSE;
                }
                strings_push(arena, dst, val);
                lex_skip_ws(&lx);
                if (lex_peek(&lx) == ',') {
                    lx.p++;
                }
            }
        } else {
            /* integer or anything else — the only non-string scalar the
             * schema carries is not one the unit needs, so consume it */
            const char *start = lx.p;
            while (lx.p < lx.end && *lx.p != '\n' && *lx.p != '#') {
                lx.p++;
            }
            while (lx.p > start && (lx.p[-1] == ' ' || lx.p[-1] == '\t' || lx.p[-1] == '\r')) {
                lx.p--;
            }
            if (lx.p == start) {
                err_set(errbuf, errbuf_len, "expected value", lx.line);
                return PM_METAL_BUILD_ERR_PARSE;
            }
        }
    }

    if (sources.oom || includes.oom || defines.oom || depends.oom) {
        err_set(errbuf, errbuf_len, "arena exhausted", lx.line);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    unit->sources = sources.items;
    unit->n_sources = sources.n;
    unit->include_dirs = includes.items;
    unit->n_include_dirs = includes.n;
    unit->defines = defines.items;
    unit->n_defines = defines.n;
    unit->depends = depends.items;
    unit->n_depends = depends.n;

    if (unit->fqn[0] == '\0' || unit->impl[0] == '\0') {
        err_set(errbuf, errbuf_len, "manifest missing fqn or impl", lx.line);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    return PM_METAL_BUILD_OK;
}

/*------------------ graph ------------------*/

static int unit_has_all_deps(const pm_metal_build_unit_t *u,
    const pm_metal_build_unit_t **done, uint32_t n_done) {
    uint32_t i, j;
    for (i = 0; i < u->n_depends; i++) {
        int found = 0;
        for (j = 0; j < n_done; j++) {
            if (strcmp(done[j]->fqn, u->depends[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

int32_t pm_metal_build_graph_resolve(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t *units, uint32_t n_units,
    const pm_metal_build_unit_t ***order, uint32_t *n_order,
    char *errbuf, size_t errbuf_len) {
    const pm_metal_build_unit_t **out = NULL;
    uint32_t n_done = 0;
    uint32_t i, j;

    if (arena == NULL || units == NULL || order == NULL || n_order == NULL) {
        err_set(errbuf, errbuf_len, "null argument", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    if (n_units == 0) {
        *order = NULL;
        *n_order = 0;
        return PM_METAL_BUILD_OK;
    }
    out = (const pm_metal_build_unit_t **)pm_util_mem_alloc(
        arena, n_units * sizeof(const pm_metal_build_unit_t *));
    if (out == NULL) {
        err_set(errbuf, errbuf_len, "arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }

    /* duplicate fqn check first — a graph with two of the same node cannot
     * be ordered and every later lookup would be ambiguous */
    for (i = 0; i < n_units; i++) {
        for (j = i + 1u; j < n_units; j++) {
            if (strcmp(units[i].fqn, units[j].fqn) == 0) {
                err_set(errbuf, errbuf_len, units[i].fqn, 0);
                return PM_METAL_BUILD_ERR_CYCLE;
            }
        }
    }

    /* Kahn's algorithm by selection: repeatedly emit any unit whose deps are
     * all already emitted. Deterministic (first match wins). */
    while (n_done < n_units) {
        uint32_t progressed = 0;
        for (i = 0; i < n_units; i++) {
            int emitted = 0;
            for (j = 0; j < n_done; j++) {
                if (out[j] == &units[i]) {
                    emitted = 1;
                    break;
                }
            }
            if (emitted) {
                continue;
            }
            if (unit_has_all_deps(&units[i], out, n_done)) {
                /* every named dep must exist somewhere in the set */
                uint32_t d;
                for (d = 0; d < units[i].n_depends; d++) {
                    int exists = 0;
                    for (j = 0; j < n_units; j++) {
                        if (strcmp(units[j].fqn, units[i].depends[d]) == 0) {
                            exists = 1;
                            break;
                        }
                    }
                    if (!exists) {
                        err_set(errbuf, errbuf_len, units[i].depends[d], 0);
                        return PM_METAL_BUILD_ERR_MISSING_DEP;
                    }
                }
                out[n_done++] = &units[i];
                progressed = 1;
            }
        }
        if (!progressed) {
            err_set(errbuf, errbuf_len, "dependency cycle", 0);
            return PM_METAL_BUILD_ERR_CYCLE;
        }
    }

    *order = out;
    *n_order = n_done;
    return PM_METAL_BUILD_OK;
}

/*------------------ compile + link (Phase 3/4) ------------------*/

/* join(unit_root, rel) into arena storage; rel may be absolute already. */
static const char *join_path(pm_util_mem_arena_t *arena, const char *root,
    const char *rel) {
    size_t rl, el;
    char *out;
    if (rel == NULL || rel[0] == '\0') {
        return NULL;
    }
    if (rel[0] == '/' || root == NULL || root[0] == '\0') {
        return dup_str(arena, rel, strlen(rel));
    }
    rl = strlen(root);
    el = strlen(rel);
    out = (char *)pm_util_mem_alloc(arena, rl + 1u + el + 1u);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, root, rl);
    out[rl] = '/';
    memcpy(out + rl + 1u, rel, el);
    out[rl + 1u + el] = '\0';
    return out;
}

/* compile_source: drive the jit.c card's TCC object path (TCC_OUTPUT_OBJ →
 * ET_REL .o bytes in the arena), forwarding the unit's include_dirs (joined
 * with unit_root) and defines. Native seats only — the wasm32 browser cell
 * has no ELF object output and jit.c reports that via errbuf. */
int32_t pm_metal_build_compile_source(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root, const char *source,
    uint8_t **obj_out, size_t *obj_len, char *errbuf, size_t errbuf_len) {
    const char **includes = NULL;
    uint32_t n_includes = 0;
    uint32_t i;

    if (arena == NULL || unit == NULL || source == NULL || source[0] == '\0'
        || obj_out == NULL || obj_len == NULL) {
        err_set(errbuf, errbuf_len, "compile_source: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (unit->n_include_dirs > 0) {
        includes = (const char **)pm_util_mem_alloc(
            arena, unit->n_include_dirs * sizeof(const char *));
        if (includes == NULL) {
            err_set(errbuf, errbuf_len, "compile_source: arena exhausted", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_include_dirs; i++) {
            includes[i] = join_path(arena, unit_root, unit->include_dirs[i]);
            if (includes[i] == NULL) {
                err_set(errbuf, errbuf_len, "compile_source: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
            n_includes++;
        }
    }
    return pm_metal_jit_c_object_compile_opts(arena, source, strlen(source),
        includes, n_includes, unit->defines, unit->n_defines,
        obj_out, obj_len, errbuf, errbuf_len) == 0
        ? PM_METAL_BUILD_OK : PM_METAL_BUILD_ERR_COMPILE;
}

#ifdef PM_METAL_BUILD_HAS_ELF
#include "pymergetic/wasmmod/pack/format/elf/load.h"
#endif

/* Process resolver (Phase 4.3): resolve a rebuilt card's true externals
 * (tcc_new, malloc, registry functions) against the already-linked process
 * copies. dlopen(NULL) — the global symbol table of the running process,
 * RTLD_LAZY so no eager relocation of every loaded object — not RTLD_DEFAULT
 * because the latter is a dlsym-side constant of a different lookup mode
 * that some platforms restrict to global-scope queries only.
 *
 * On x86_64 the linked image is mapped MAP_32BIT (~<4GiB) while process
 * symbols sit at 0x7f...: a direct R_X86_64_PLT32 call would truncate its
 * 32-bit displacement. Every far target is answered with a synthesized
 * movabs+jmp thunk from ONE pre-allocated RWX thunk table — allocated before
 * the loader starts so it never moves (earlier relocs already point in). */
#define PM_BUILD_THUNK_SLOTS 512u
#define PM_BUILD_THUNK_BYTES 16u

typedef struct pm_build_resolve_ctx {
#ifdef PM_METAL_BUILD_HAS_ELF
    uint8_t *thunk_base;
    size_t thunk_used;
    int failed;
#else
    char _pad;
#endif
} pm_build_resolve_ctx_t;

#ifdef PM_METAL_BUILD_HAS_ELF
#include <dlfcn.h>
#include <sys/mman.h>

static void thunk_ctx_init(pm_build_resolve_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    /* MAP_32BIT like the loader's image mapping: the image's PLT32 relocs
     * point here with a signed 32-bit displacement, so the table must sit
     * in the same low address span. No fallback: without it links would
     * silently truncate (better an honest link error). */
    ctx->thunk_base = (uint8_t *)mmap(NULL,
        (size_t)PM_BUILD_THUNK_SLOTS * PM_BUILD_THUNK_BYTES,
        PROT_READ | PROT_WRITE | PROT_EXEC,
#if defined(__x86_64__) && defined(MAP_32BIT)
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
#else
        MAP_PRIVATE | MAP_ANONYMOUS,
#endif
        -1, 0);
    ctx->thunk_used = 0;
    ctx->failed = ctx->thunk_base == MAP_FAILED;
    if (ctx->failed) {
        ctx->thunk_base = NULL;
    }
}

static void thunk_ctx_deinit(pm_build_resolve_ctx_t *ctx) {
    if (ctx != NULL && ctx->thunk_base != NULL) {
        munmap(ctx->thunk_base,
            (size_t)PM_BUILD_THUNK_SLOTS * PM_BUILD_THUNK_BYTES);
        ctx->thunk_base = NULL;
    }
}

/* Executable ranges of this process, from /proc/self/maps. A far DATA symbol
 * (stderr, environ) must be returned raw: GOT slots hold full 64-bit
 * addresses, so nothing truncates. Only far CODE needs a thunk, because a
 * direct `call rel32` cannot reach 0x7f... from a MAP_32BIT image. */
typedef struct pm_build_exec_range {
    uintptr_t lo;
    uintptr_t hi;
} pm_build_exec_range_t;

static pm_build_exec_range_t pm_build_exec_ranges[512];
static uint32_t pm_build_n_exec_ranges;
static int pm_build_exec_ranges_ready;

static void pm_build_exec_ranges_load(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (f == NULL) {
        pm_build_exec_ranges_ready = 1;
        return;
    }
    while (pm_build_n_exec_ranges
        < (uint32_t)(sizeof(pm_build_exec_ranges) / sizeof(pm_build_exec_ranges[0]))) {
        char line[512];
        unsigned long lo, hi;
        char perms[8];
        if (fgets(line, sizeof(line), f) == NULL) {
            break;
        }
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) {
            continue;
        }
        if (perms[2] != 'x') {
            continue;
        }
        pm_build_exec_ranges[pm_build_n_exec_ranges].lo = (uintptr_t)lo;
        pm_build_exec_ranges[pm_build_n_exec_ranges].hi = (uintptr_t)hi;
        pm_build_n_exec_ranges++;
    }
    fclose(f);
    pm_build_exec_ranges_ready = 1;
}

static int pm_build_addr_is_code(uintptr_t a) {
    uint32_t i;
    if (!pm_build_exec_ranges_ready) {
        pm_build_exec_ranges_load();
    }
    for (i = 0; i < pm_build_n_exec_ranges; i++) {
        if (a >= pm_build_exec_ranges[i].lo && a < pm_build_exec_ranges[i].hi) {
            return 1;
        }
    }
    return 0;
}

static void *proc_resolve(const char *name, void *ctx_in) {
    pm_build_resolve_ctx_t *ctx = (pm_build_resolve_ctx_t *)ctx_in;
    void *h = dlopen(NULL, RTLD_LAZY);
    void *p = h != NULL ? dlsym(h, name) : NULL;
    if (p == NULL) {
        return NULL;
    }
    if (ctx == NULL || ctx->thunk_base == NULL) {
        return p;
    }
    if (!pm_build_addr_is_code((uintptr_t)p)) {
        return p;
    }
    {
        ptrdiff_t delta = (uint8_t *)p - (ctx->thunk_base + ctx->thunk_used);
        /* PLT32 carries a signed 32-bit displacement; keep a wide margin. */
        if (delta > (ptrdiff_t)INT32_MIN / 2 && delta < (ptrdiff_t)INT32_MAX / 2) {
            return p;
        }
    }
    if (ctx->thunk_used >= (size_t)PM_BUILD_THUNK_SLOTS * PM_BUILD_THUNK_BYTES) {
        ctx->failed = 1;
        return NULL;
    }
    {
        uint8_t *t = ctx->thunk_base + ctx->thunk_used;
        /* movabs $target, %rax ; jmp *%rax */
        t[0] = 0x48; t[1] = 0xb8;
        memcpy(t + 2, &p, sizeof(p));
        t[10] = 0xff; t[11] = 0xe0;
        ctx->thunk_used += PM_BUILD_THUNK_BYTES;
        return t;
    }
}
#endif /* PM_METAL_BUILD_HAS_ELF */

int32_t pm_metal_build_link(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, uint8_t **objects, const size_t *lens,
    uint32_t n_objects, pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len) {
#ifdef PM_METAL_BUILD_HAS_ELF
    mp_wasm_elf_image_t *img = NULL;
    char err[PM_METAL_BUILD_ERR_MAX];
    uint32_t i;
    uint32_t *lens32;
    pm_build_resolve_ctx_t rctx;

    if (arena == NULL || unit == NULL || objects == NULL || lens == NULL
        || n_objects == 0 || artifact == NULL) {
        err_set(errbuf, errbuf_len, "link: bad args", 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    /* the multi loader takes uint32 lens; our artifacts are small */
    lens32 = (uint32_t *)pm_util_mem_alloc(arena, n_objects * sizeof(uint32_t));
    if (lens32 == NULL) {
        err_set(errbuf, errbuf_len, "link: arena alloc failed", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    for (i = 0; i < n_objects; i++) {
        lens32[i] = (uint32_t)lens[i];
    }
    memset(&rctx, 0, sizeof(rctx));
    thunk_ctx_init(&rctx);
    if (!mp_wasm_elf_image_load_multi((const uint8_t *const *)objects, lens32,
        n_objects, proc_resolve, &rctx, &img, err, sizeof(err))) {
        thunk_ctx_deinit(&rctx);
        err_set(errbuf, errbuf_len, err, 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    /* The image is mmap'd (not arena memory): publish it through the
     * artifact so the caller can lookup and free it. The thunk table is
     * leaked deliberately: the image's code points into it, so it must
     * outlive the artifact. */
    memset(artifact, 0, sizeof(*artifact));
    snprintf(artifact->fqn, sizeof(artifact->fqn), "%s", unit->fqn);
    artifact->bytes = (uint8_t *)img;
    artifact->len = img->size;
    artifact->is_wasm = 0;
    return PM_METAL_BUILD_OK;
#else
    (void)arena; (void)unit; (void)objects; (void)lens; (void)n_objects;
    (void)artifact;
    err_set(errbuf, errbuf_len, "link: no ELF loader on this seat", 0);
    return PM_METAL_BUILD_ERR_LINK;
#endif
}

void pm_metal_build_artifact_destroy(pm_metal_build_artifact_t *artifact) {
#ifdef PM_METAL_BUILD_HAS_ELF
    if (artifact != NULL && artifact->bytes != NULL) {
        mp_wasm_elf_image_free((mp_wasm_elf_image_t *)artifact->bytes);
        artifact->bytes = NULL;
        artifact->len = 0;
    }
#else
    (void)artifact;
#endif
}

void *pm_metal_build_artifact_lookup(const pm_metal_build_artifact_t *artifact,
    const char *name) {
#ifdef PM_METAL_BUILD_HAS_ELF
    if (artifact == NULL || artifact->bytes == NULL || name == NULL) {
        return NULL;
    }
    return mp_wasm_elf_lookup((const mp_wasm_elf_image_t *)artifact->bytes, name);
#else
    (void)artifact; (void)name;
    return NULL;
#endif
}

#include "pymergetic/wasmmod/guest.h"

/*------------------ runtime card discovery (Phase 4.4) ------------------
 * The embedded card table (tools/embed_src.py -> src_embed.inc.h, included
 * by the inspect card on every seat) is the source of truth: each entry
 * carries the card's impl and raw __pmm__.toml bytes, so discovery is pure
 * data — no filesystem walk, identical on every seat. */

int32_t pm_metal_build_discover(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t **units, uint32_t *n_units,
    char *errbuf, size_t errbuf_len) {
    uint32_t n = pm_metal_src_card_count();
    pm_metal_build_unit_t *out;
    uint32_t i;
    uint32_t w = 0;

    if (arena == NULL || units == NULL || n_units == NULL) {
        err_set(errbuf, errbuf_len, "discover: bad args", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    *units = NULL;
    *n_units = 0;
    if (n == 0) {
        return PM_METAL_BUILD_OK;
    }
    out = (pm_metal_build_unit_t *)pm_util_mem_alloc(
        arena, n * sizeof(pm_metal_build_unit_t));
    if (out == NULL) {
        err_set(errbuf, errbuf_len, "discover: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    for (i = 0; i < n; i++) {
        const pm_metal_src_card_t *c = &PM_METAL_SRC_CARDS[i];
        char err[PM_METAL_BUILD_ERR_MAX];
        if (c->toml == NULL) {
            continue;
        }
        if (pm_metal_build_unit_parse(arena, (const uint8_t *)c->toml,
            strlen(c->toml), &out[w], err, sizeof(err)) != PM_METAL_BUILD_OK) {
            continue;  /* a broken manifest is skipped, not fatal */
        }
        /* sources: the card's embedded muscle file names (the parse filled
         * everything else; a card unit has no extra relative paths). */
        {
            const char **srcs = (const char **)pm_util_mem_alloc(
                arena, c->nfiles * sizeof(const char *));
            uint32_t f;
            if (srcs == NULL) {
                err_set(errbuf, errbuf_len, "discover: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
            for (f = 0; f < c->nfiles; f++) {
                srcs[f] = c->files[f].rel;
            }
            out[w].sources = srcs;
            out[w].n_sources = c->nfiles;
        }
        w++;
    }
    *units = out;
    *n_units = w;
    return PM_METAL_BUILD_OK;
}

int32_t pm_metal_build_unit_compile(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **extra_defines, uint32_t n_extra_defines,
    pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len) {
    uint8_t **objs;
    size_t *lens;
    const char **all_includes = NULL;
    uint32_t n_all_includes = 0;
    uint32_t i;
    uint32_t obj_i;
    int32_t rc;
    pm_metal_build_record_t *rec = NULL;

    if (arena == NULL || unit == NULL || unit_root == NULL || artifact == NULL) {
        err_set(errbuf, errbuf_len, "unit_compile: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (strcmp(unit->impl, "c") != 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "not yet buildable: impl=%s", unit->impl);
        err_set(errbuf, errbuf_len, msg, 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (unit->n_sources == 0) {
        err_set(errbuf, errbuf_len, "unit_compile: no sources", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    objs = (uint8_t **)pm_util_mem_alloc(
        arena, unit->n_sources * sizeof(uint8_t *));
    lens = (size_t *)pm_util_mem_alloc(arena, unit->n_sources * sizeof(size_t));
    if (objs == NULL || lens == NULL) {
        err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }

    /* merge unit->include_dirs (rooted at unit_root) + the seat fill */
    n_all_includes = unit->n_include_dirs + n_include_dirs;
    if (n_all_includes > 0) {
        all_includes = (const char **)pm_util_mem_alloc(
            arena, n_all_includes * sizeof(const char *));
        if (all_includes == NULL) {
            err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_include_dirs; i++) {
            all_includes[i] = join_path(arena, unit_root, unit->include_dirs[i]);
            if (all_includes[i] == NULL) {
                err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
        for (i = 0; i < n_include_dirs; i++) {
            all_includes[unit->n_include_dirs + i] = include_dirs[i];
        }
    }

    /* compile each embedded source through the opts seam (unit defines +
     * the seat fill's defines joined) */
    {
        const char **all_defines = NULL;
        uint32_t n_all_defines = unit->n_defines + n_extra_defines;
        if (n_all_defines > 0) {
            all_defines = (const char **)pm_util_mem_alloc(
                arena, n_all_defines * sizeof(const char *));
            if (all_defines == NULL) {
                err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
            for (i = 0; i < unit->n_defines; i++) {
                all_defines[i] = unit->defines[i];
            }
            for (i = 0; i < n_extra_defines; i++) {
                all_defines[unit->n_defines + i] = extra_defines[i];
            }
        }
        for (obj_i = 0; obj_i < unit->n_sources; obj_i++) {
            const pm_metal_src_card_t *c = pm_metal_src_find(unit->fqn);
            const char *src = NULL;
            if (c != NULL) {
                uint32_t f;
                for (f = 0; f < c->nfiles; f++) {
                    if (strcmp(c->files[f].rel, unit->sources[obj_i]) == 0) {
                        src = (const char *)c->files[f].data;
                        break;
                    }
                }
            }
            if (src == NULL) {
                err_set(errbuf, errbuf_len, "unit_compile: source not in embed", 0);
                return PM_METAL_BUILD_ERR_COMPILE;
            }
            if (pm_metal_jit_c_object_compile_opts(arena, src, strlen(src),
                all_includes, n_all_includes, all_defines, n_all_defines,
                &objs[obj_i], &lens[obj_i], errbuf, errbuf_len) != 0) {
                return PM_METAL_BUILD_ERR_COMPILE;
            }
        }
    }

    rc = pm_metal_build_link(arena, unit, objs, lens, unit->n_sources,
        artifact, errbuf, errbuf_len);
    if (rc != PM_METAL_BUILD_OK) {
        return rc;
    }

    /* provenance record: source paths + object lengths + linked symbols.
     * Best-effort — a record overflow truncates the lists, never fails the
     * build (the artifact is the product; the record is the audit trail). */
    rec = record_slot_acquire(unit->fqn);
    if (rec != NULL) {
        uint32_t cap = unit->n_sources;
        if (cap > PM_METAL_BUILD_MAX_OBJS) {
            cap = PM_METAL_BUILD_MAX_OBJS;
        }
        for (i = 0; i < cap; i++) {
            snprintf(rec->src_paths[i], PM_METAL_BUILD_MAX_SRC_PATH, "%s",
                unit->sources[i]);
            rec->obj_lens[i] = (uint32_t)lens[i];
        }
        rec->n_sources = cap;
#ifdef PM_METAL_BUILD_HAS_ELF
        {
            pm_build_rec_sym_ctx_t sctx;
            sctx.r = rec;
            sctx.w = 0;
            mp_wasm_elf_foreach_func(
                (const mp_wasm_elf_image_t *)artifact->bytes,
                record_sym_cb, &sctx);
            rec->n_syms = sctx.w;
        }
#endif
    }
    return PM_METAL_BUILD_OK;
}

PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_unit_parse, pm_metal_build_unit_parse,
    int32_t(pm_util_mem_arena_t *, const uint8_t *, size_t, pm_metal_build_unit_t *,
        char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_graph_resolve, pm_metal_build_graph_resolve,
    int32_t(pm_util_mem_arena_t *, pm_metal_build_unit_t *, uint32_t,
        const pm_metal_build_unit_t ***, uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_compile_source, pm_metal_build_compile_source,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, const char *,
        const char *, uint8_t **, size_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_link, pm_metal_build_link,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, uint8_t **,
        const size_t *, uint32_t, pm_metal_build_artifact_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_discover, pm_metal_build_discover,
    int32_t(pm_util_mem_arena_t *, pm_metal_build_unit_t **, uint32_t *,
        char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_unit_compile, pm_metal_build_unit_compile,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, const char *,
        const char **, uint32_t, const char **, uint32_t,
        pm_metal_build_artifact_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_record_find, pm_metal_build_record_find,
    const pm_metal_build_record_t *(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_record_reset, pm_metal_build_record_reset,
    void(void));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_destroy, pm_metal_build_artifact_destroy,
    void(pm_metal_build_artifact_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_lookup, pm_metal_build_artifact_lookup,
    void *(const pm_metal_build_artifact_t *, const char *));
