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

#include <stdio.h>
#include <string.h>

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

/*------------------ compile + link (Phase 3) ------------------*/

/* compile_source: drive the jit.c card's TCC object path (TCC_OUTPUT_OBJ →
 * ET_REL .o bytes in the arena). Native seats only — the wasm32 browser cell
 * has no ELF object output and jit.c reports that via errbuf. */
int32_t pm_metal_build_compile_source(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *source,
    uint8_t **obj_out, size_t *obj_len, char *errbuf, size_t errbuf_len) {
    if (arena == NULL || unit == NULL || source == NULL || source[0] == '\0'
        || obj_out == NULL || obj_len == NULL) {
        err_set(errbuf, errbuf_len, "compile_source: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    return pm_metal_jit_c_object_compile(arena, source, strlen(source),
        obj_out, obj_len, errbuf, errbuf_len) == 0
        ? PM_METAL_BUILD_OK : PM_METAL_BUILD_ERR_COMPILE;
}

#ifdef PM_METAL_BUILD_HAS_ELF
#include "pymergetic/wasmmod/pack/format/elf/load.h"
#endif

int32_t pm_metal_build_link(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, uint8_t **objects, const size_t *lens,
    uint32_t n_objects, pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len) {
#ifdef PM_METAL_BUILD_HAS_ELF
    mp_wasm_elf_image_t *img = NULL;
    char err[PM_METAL_BUILD_ERR_MAX];
    uint32_t i;
    uint32_t *lens32;

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
    if (!mp_wasm_elf_image_load_multi((const uint8_t *const *)objects, lens32,
        n_objects, NULL, NULL, &img, err, sizeof(err))) {
        err_set(errbuf, errbuf_len, err, 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    /* The image is mmap'd (not arena memory): publish it through the
     * artifact so the caller can lookup and free it. */
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

PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_unit_parse, pm_metal_build_unit_parse,
    int32_t(pm_util_mem_arena_t *, const uint8_t *, size_t, pm_metal_build_unit_t *,
        char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_graph_resolve, pm_metal_build_graph_resolve,
    int32_t(pm_util_mem_arena_t *, pm_metal_build_unit_t *, uint32_t,
        const pm_metal_build_unit_t ***, uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_compile_source, pm_metal_build_compile_source,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, const char *,
        uint8_t **, size_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_link, pm_metal_build_link,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, uint8_t **,
        const size_t *, uint32_t, pm_metal_build_artifact_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_destroy, pm_metal_build_artifact_destroy,
    void(pm_metal_build_artifact_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_lookup, pm_metal_build_artifact_lookup,
    void *(const pm_metal_build_artifact_t *, const char *));
