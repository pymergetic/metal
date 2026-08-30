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

#include "pymergetic/wasmmod/registry.h"
#include "pymergetic/metal/inspect/__exports__.h"

/* WASM-seat link path: the loader is RS (loader/__impl__.rs) on every seat,
 * but only the wasm-container seats need it for linking — ELF seats link
 * through the in-tree relocator instead. */
#if PM_HAS_TCC && defined(TCC_TARGET_WASM32) && !defined(PM_METAL_BUILD_HAS_ELF)
#define PM_METAL_BUILD_WASM_LINK 1
#include "pymergetic/wasmmod/loader/__exports__.h"
#endif

#include <stdio.h>
#include <string.h>

/* strtoul lives in stdlib.h; the firmware seats get it through their own
 * libc shims. */
#include <stdlib.h>

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

/*------------------ change ledger (fs-backed, JSON-lines) ------------------
 * One file in the fs card: /src/.changes.jsonl. note_add appends a JSON line
 * (read the file, drop it, re-add with the new bytes — fs_add refuses
 * duplicates, so append is a read-modify-write). notes_query scans lines and
 * concatenates matches. The ledger is seeded from the authored
 * changes.jsonl beside the muscle (source-in-its-lang: the seed is a real
 * file, embedded as bytes, never a C string). */

#include "pymergetic/metal/fs/__exports__.h"
#include "pymergetic/metal/build/changes_embed.inc.h"

#define PM_METAL_BUILD_LEDGER_PATH "/src/.changes.jsonl"

/* One shared scratch for every ledger read path (file read + scan). The
 * runtime is single-threaded and note_add / notes_query never nest, so a
 * single static keeps the card's BSS footprint at one ledger-sized buffer
 * (firmware seats link this card too). */
static uint8_t s_ledger_buf[PM_METAL_BUILD_LEDGER_MAX];

const char *pm_metal_build_ledger_path(void) {
    return PM_METAL_BUILD_LEDGER_PATH;
}

/* Materialize the ledger file into fs at first use (idempotent). Returns 0
 * when the file exists (now or before), -1 when it cannot be created. */
static int32_t ledger_ensure(void) {
    uint32_t len = 0;
    int32_t st = pm_metal_fs_stat(PM_METAL_BUILD_LEDGER_PATH, &len);
    if (st == 0) {
        return len > 0 ? 0 : -1;
    }
    return pm_metal_fs_add(PM_METAL_BUILD_LEDGER_PATH,
        (const uint8_t *)pm_metal_build_changes_jsonl(),
        pm_metal_build_changes_jsonl_len()) < 0 ? -1 : 0;
}

/* Portable bounded substring find (memmem is a GNU extension; firmware
 * freestanding has neither). Returns the match or NULL. */
static const char *build_memfind(const char *hay, size_t hay_n,
    const char *needle) {
    size_t nn = strlen(needle);
    size_t i;
    if (nn == 0 || hay_n < nn) {
        return NULL;
    }
    for (i = 0; i + nn <= hay_n; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, nn) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

/* JSON string escape into out (bounded). Returns bytes written. */
static size_t note_esc(const char *s, char *out, size_t out_max) {
    size_t w = 0;
    for (; *s != 0 && w + 7u < out_max; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out[w++] = '\\';
            out[w++] = (char)c;
        } else if (c == '\n') {
            out[w++] = '\\';
            out[w++] = 'n';
        } else if (c == '\r') {
            out[w++] = '\\';
            out[w++] = 'r';
        } else if (c == '\t') {
            out[w++] = '\\';
            out[w++] = 't';
        } else if (c < 0x20u) {
            w += (size_t)snprintf(out + w, out_max - w, "\\u%04x", c);
        } else {
            out[w++] = (char)c;
        }
    }
    out[w] = 0;
    return w;
}

static const char *note_kind_name(pm_metal_build_note_kind_t kind) {
    switch (kind) {
        case PM_METAL_BUILD_NOTE_CHANGE:   return "change";
        case PM_METAL_BUILD_NOTE_DECISION: return "decision";
        case PM_METAL_BUILD_NOTE_WARNING:  return "warning";
        case PM_METAL_BUILD_NOTE_TODO:     return "todo";
        default:                           return NULL;
    }
}

int32_t pm_metal_build_note_add(const char *target,
    pm_metal_build_note_kind_t kind, const char *reason,
    const char *const *refs, uint32_t n_refs) {
    static char line[512 + PM_METAL_BUILD_NOTE_REFS_MAX * 80u];
    char esc[2 * PM_METAL_BUILD_NOTE_REASON_MAX];
    const char *kname;
    size_t w = 0;
    uint32_t existing_len = 0;
    uint32_t i;
    if (target == NULL || target[0] == 0 || reason == NULL || reason[0] == 0) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    kname = note_kind_name(kind);
    if (kname == NULL) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    if (n_refs > PM_METAL_BUILD_NOTE_REFS_MAX) {
        n_refs = PM_METAL_BUILD_NOTE_REFS_MAX;
    }
    if (ledger_ensure() != 0) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    w = (size_t)snprintf(line, sizeof(line), "{\"kind\":\"%s\",\"target\":\"%s\",\"reason\":\"",
        kname, target);
    if (w >= sizeof(line)) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    note_esc(reason, esc, sizeof(esc));
    w += (size_t)snprintf(line + w, sizeof(line) - w, "%s\"", esc);
    if (w >= sizeof(line)) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    if (n_refs > 0) {
        int wrote_any = 0;
        w += (size_t)snprintf(line + w, sizeof(line) - w, ",\"refs\":[");
        for (i = 0; i < n_refs && w < sizeof(line); i++) {
            if (refs[i] == NULL) {
                continue;
            }
            if (wrote_any) {
                w += (size_t)snprintf(line + w, sizeof(line) - w, ",");
            }
            note_esc(refs[i], esc, sizeof(esc));
            w += (size_t)snprintf(line + w, sizeof(line) - w, "\"%s\"", esc);
            wrote_any = 1;
        }
        w += (size_t)snprintf(line + w, sizeof(line) - w, "]");
    }
    if (w >= sizeof(line)) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    w += (size_t)snprintf(line + w, sizeof(line) - w, "}\n");
    if (w >= sizeof(line)) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    /* read-modify-write append: fs_add refuses an existing path */
    {
        uint32_t got = 0;
        uint8_t *existing = s_ledger_buf;
        if (pm_metal_fs_stat(PM_METAL_BUILD_LEDGER_PATH, &existing_len) != 0) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        if (existing_len > 0) {
            got = existing_len;
            if (got > sizeof(s_ledger_buf)) {
                got = sizeof(s_ledger_buf);
            }
            if (pm_metal_fs_read(PM_METAL_BUILD_LEDGER_PATH, existing, &got) != 0) {
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
        if (got + w >= sizeof(s_ledger_buf)) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        if (pm_metal_fs_drop(PM_METAL_BUILD_LEDGER_PATH) != 0 && existing_len > 0) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        memcpy(existing + got, line, w);
        if (pm_metal_fs_add(PM_METAL_BUILD_LEDGER_PATH, existing, got + (uint32_t)w) < 0) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
    }
    return PM_METAL_BUILD_OK;
}

/* Does ledger line `ln` (length n) match target + kind? target NULL = all. */
static int note_line_match(const char *ln, size_t n, const char *target,
    int32_t kind) {
    char pat[PM_METAL_BUILD_NOTE_TARGET_MAX + 8u];
    if (kind >= 0) {
        const char *kname = note_kind_name((pm_metal_build_note_kind_t)kind);
        if (kname == NULL) {
            return 0;
        }
        snprintf(pat, sizeof(pat), "\"kind\":\"%s\"", kname);
        if (build_memfind(ln, n, pat) == NULL) {
            return 0;
        }
    }
    if (target == NULL) {
        return 1;
    }
    snprintf(pat, sizeof(pat), "\"target\":\"%s\"", target);
    return build_memfind(ln, n, pat) != NULL;
}

int32_t pm_metal_build_notes_query(const char *target,
    int32_t kind, char *out, size_t out_len, uint32_t *out_n) {
    uint32_t len = 0;
    uint32_t n_match = 0;
    size_t w = 0;
    const char *p;
    const char *end;
    uint8_t *buf = s_ledger_buf;
    if (out == NULL || out_len == 0 || out_n == NULL) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    out[0] = 0;
    *out_n = 0;
    if (ledger_ensure() != 0) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    len = sizeof(s_ledger_buf);
    if (pm_metal_fs_read(PM_METAL_BUILD_LEDGER_PATH, buf, &len) != 0) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    p = (const char *)buf;
    end = p + len;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        size_t lnlen = nl != NULL ? (size_t)(nl - p) : (size_t)(end - p);
        if (lnlen > 0 && note_line_match(p, lnlen, target, kind)) {
            if (n_match > 0 && w + 1u < out_len) {
                out[w++] = '\n';
            }
            if (lnlen >= out_len - w) {
                lnlen = out_len - w - 1u;
            }
            memcpy(out + w, p, lnlen);
            w += lnlen;
            out[w] = 0;
            n_match++;
        }
        p = nl != NULL ? nl + 1 : end;
    }
    *out_n = n_match;
    return (int32_t)n_match;
}

int32_t pm_metal_build_note_has(const char *target,
    pm_metal_build_note_kind_t kind) {
    char scratch[128];
    uint32_t n = 0;
    if (target == NULL || target[0] == 0) {
        return 0;
    }
    if (pm_metal_build_notes_query(target, (int32_t)kind, scratch,
            sizeof(scratch), &n) < 0) {
        return 0;
    }
    return n > 0 ? 1 : 0;
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
#ifdef PM_METAL_BUILD_WASM_LINK
    /* wasm-seat link: each object IS a wasm module (the jit.c wasm32 object
     * path). "Linking" = load every module through the loader, which
     * instantiates it and publishes its named exports into the registry —
     * the software-defined linker. Cross-module calls resolve through the
     * registry (connect_import), not through a merged image. */
    uint32_t i;
    if (arena == NULL || unit == NULL || objects == NULL || lens == NULL
        || n_objects == 0 || artifact == NULL) {
        err_set(errbuf, errbuf_len, "link: bad args", 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    if (n_objects > PM_METAL_BUILD_MAX_OBJS) {
        err_set(errbuf, errbuf_len, "link: too many objects", 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    memset(artifact, 0, sizeof(*artifact));
    snprintf(artifact->fqn, sizeof(artifact->fqn), "%s", unit->fqn);
    artifact->is_wasm = 1;
    /* rebuild contract (same shape as the ELF seat's munmap): destroy the
     * previous artifact before linking a new one — the loader publishes
     * under the unit fqn and a live previous module would shadow it. */
    for (i = 0; i < n_objects; i++) {
        pm_wasmmod_registry_handle_t h;
        if (objects[i] == NULL || lens[i] == 0) {
            err_set(errbuf, errbuf_len, "link: empty object", 0);
            pm_metal_build_artifact_destroy(artifact);
            return PM_METAL_BUILD_ERR_LINK;
        }
        h = pm_wasmmod_loader_load(
            (const uint8_t *)unit->fqn, (uint32_t)strlen(unit->fqn),
            objects[i], (uint32_t)lens[i]);
        if (h.index == UINT32_MAX) {
            char msg[PM_METAL_BUILD_ERR_MAX];
            snprintf(msg, sizeof(msg), "link: loader refused object %u", (unsigned)i);
            err_set(errbuf, errbuf_len, msg, 0);
            pm_metal_build_artifact_destroy(artifact);
            return PM_METAL_BUILD_ERR_LINK;
        }
        artifact->loader_handles[i] = h;
        artifact->n_loader_handles++;
    }
    return PM_METAL_BUILD_OK;
#else
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
#endif /* PM_METAL_BUILD_WASM_LINK */
}

void pm_metal_build_artifact_destroy(pm_metal_build_artifact_t *artifact) {
#ifdef PM_METAL_BUILD_WASM_LINK
    if (artifact != NULL && artifact->n_loader_handles > 0) {
        uint32_t i;
        for (i = 0; i < artifact->n_loader_handles; i++) {
            (void)pm_wasmmod_loader_unload(artifact->loader_handles[i]);
        }
        artifact->n_loader_handles = 0;
    }
#elif defined(PM_METAL_BUILD_HAS_ELF)
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
#ifdef PM_METAL_BUILD_WASM_LINK
    /* wasm seat: the module's exports live in the registry (published by the
     * loader at link time). Lookup walks the registry's export table for the
     * unit fqn; the honest handle is the sentinel 1 — calling goes through
     * pm_wasmmod_registry_call, which owns the wasm trampoline. */
    if (artifact == NULL || artifact->fqn[0] == '\0' || name == NULL) {
        return NULL;
    }
    {
        uint32_t n = pm_wasmmod_registry_export_count(
            (const uint8_t *)artifact->fqn,
            (uint32_t)strlen(artifact->fqn));
        uint32_t i;
        for (i = 0; i < n; i++) {
            char ename[96];
            uint32_t elen = sizeof(ename);
            pm_wasmmod_registry_export_kind_t kind =
                PM_WASMMOD_REGISTRY_EXPORT_FN;
            if (pm_wasmmod_registry_export_at(
                    (const uint8_t *)artifact->fqn,
                    (uint32_t)strlen(artifact->fqn), i,
                    (uint8_t *)ename, &elen, &kind, NULL, 0) == 0) {
                continue;
            }
            if (strcmp(ename, name) == 0) {
                return (void *)(uintptr_t)1;
            }
        }
        return NULL;
    }
#elif defined(PM_METAL_BUILD_HAS_ELF)
    if (artifact == NULL || artifact->bytes == NULL || name == NULL) {
        return NULL;
    }
    return mp_wasm_elf_lookup((const mp_wasm_elf_image_t *)artifact->bytes, name);
#else
    (void)artifact; (void)name;
    return NULL;
#endif
}

/* Call a function in a linked artifact with scalar args. args are the
 * caller's values (i64 transport; the wasm seat narrows to i32 — its C
 * ints are wasm i32), n_args their count; the result lands in *res as
 * an i64 (widened from the callee's return width). The callee's
 * arity/type contract is the caller's: this face only transports
 * scalars, it does not typecheck the target — the same posture as the C
 * feeds, which cast lookup's pointer themselves.
 *
 * Returns 0 on a completed call, negative on refusal (bad artifact, name
 * not present, or a wasm trampoline failure).
 *
 * ELF seats: lookup resolves a native code pointer and the call goes
 * through it directly (the image is already relocated and executable).
 * wasm seat: the pointer is the sentinel 1 — the honest call path is the
 * registry trampoline (pm_wasmmod_registry_call), which owns the WAMR
 * exec-env plumbing the adapter fns need. */
int32_t pm_metal_build_artifact_call(const pm_metal_build_artifact_t *artifact,
    const char *name, const int64_t *args, uint32_t n_args, int64_t *res) {
    if (res != NULL) {
        *res = 0;
    }
    if (artifact == NULL || name == NULL || n_args > 8u) {
        return -1;
    }
#ifdef PM_METAL_BUILD_WASM_LINK
    if (artifact->fqn[0] == '\0') {
        return -1;
    }
    {
        pm_wasmmod_registry_value_t wargs[8];
        pm_wasmmod_registry_value_t wres;
        uint32_t i;
        int32_t st;
        if (pm_metal_build_artifact_lookup(artifact, name) == NULL) {
            return -2;
        }
        /* i32 spine: TCC's wasm32 C lowers int params to wasm i32, and
         * WAMR packs each arg by the caller-declared kind — an i64-kind
         * arg would hand an i32 callee two cells and misalign every
         * parameter after it. The transport widens to i64 only on the
         * way back (the result union covers both). */
        for (i = 0; i < n_args; i++) {
            wargs[i].kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
            wargs[i].of.i32 = (int32_t)args[i];
        }
        wres.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
        wres.of.i32 = 0;
        st = pm_wasmmod_registry_call(
            (const uint8_t *)artifact->fqn, (uint32_t)strlen(artifact->fqn),
            (const uint8_t *)name, (uint32_t)strlen(name),
            &wargs[0], n_args, &wres, 1u);
        if (st < 0) {
            return -3;
        }
        if (res != NULL) {
            *res = (int64_t)wres.of.i32;
        }
        return 0;
    }
#elif defined(PM_METAL_BUILD_HAS_ELF)
    {
        void *p = pm_metal_build_artifact_lookup(artifact, name);
        if (p == NULL) {
            return -2;
        }
        /* one call shape per arity: int64_t(int64_t...) — the honest scalar
         * spine. Struct returns, variadics and pointers stay C-feed-only
         * until the bridge grows a type spine of its own. */
        switch (n_args) {
        case 0:
            if (res != NULL) {
                *res = ((int64_t (*)(void))p)();
            } else {
                ((void (*)(void))p)();
            }
            return 0;
        case 1:
            if (res != NULL) {
                *res = ((int64_t (*)(int64_t))p)(args[0]);
            } else {
                ((void (*)(int64_t))p)(args[0]);
            }
            return 0;
        case 2:
            if (res != NULL) {
                *res = ((int64_t (*)(int64_t, int64_t))p)(args[0], args[1]);
            } else {
                ((void (*)(int64_t, int64_t))p)(args[0], args[1]);
            }
            return 0;
        default:
            return -4;
        }
    }
#else
    (void)args;
    return -5;
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

#define PM_METAL_BUILD_AT_SLOTS 4u

typedef struct pm_build_at_slot {
    pm_metal_build_at_info_t info;
    int32_t valid;
} pm_build_at_slot_t;

static pm_build_at_slot_t s_at[PM_METAL_BUILD_AT_SLOTS];
static uint32_t s_at_epoch;

/* deps from the embedded manifest: parse the card's __pmm__.toml and copy
 * its depends[] into the info block (bounded, de-duplicated). The TOML
 * parse needs an arena; one card-local scratch serves it (the parse is
 * single-threaded and self-contained). */
static void at_fill_deps(pm_metal_build_at_info_t *info) {
    const pm_metal_src_card_t *c = pm_metal_src_find(info->fqn);
    static uint8_t deps_scratch[8192];
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t unit;
    char err[PM_METAL_BUILD_ERR_MAX];
    uint32_t i;
    if (c == NULL || c->toml == NULL) {
        return;
    }
    arena = pm_util_mem_arena_create(deps_scratch, sizeof(deps_scratch));
    if (arena == NULL) {
        return;
    }
    if (pm_metal_build_unit_parse(arena, (const uint8_t *)c->toml,
            strlen(c->toml), &unit, err, sizeof(err)) == PM_METAL_BUILD_OK) {
        for (i = 0; i < unit.n_depends && info->n_deps < PM_METAL_BUILD_AT_REFS_MAX; i++) {
            uint32_t j;
            int dup = 0;
            for (j = 0; j < info->n_deps; j++) {
                if (strcmp(info->deps[j], unit.depends[i]) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                snprintf(info->deps[info->n_deps], PM_METAL_BUILD_AT_REF_MAX,
                    "%s", unit.depends[i]);
                info->n_deps++;
            }
        }
    }
    pm_util_mem_arena_destroy(arena);
}

/* doc from the inspect extractor: JSON in, first prose line + file/line out.
 * The JSON shape is {"fqn":...,"name":...,"file":...,"line":N,"impl":...,
 * "prose":"...","params":[...],"example":"..."}. */
static void at_fill_doc(pm_metal_build_at_info_t *info) {
    const char *json = pm_metal_inspect_doc(info->fqn, info->name);
    const char *p;
    if (json == NULL) {
        return;
    }
    p = build_memfind(json, strlen(json), "\"prose\":\"");
    if (p != NULL) {
        const char *q = p + 9;
        uint32_t w = 0;
        while (*q != 0 && *q != '"' && w + 1u < sizeof(info->doc)) {
            if (q[0] == '\\' && q[1] != 0) {
                q++;
            }
            info->doc[w++] = *q++;
        }
        info->doc[w] = 0;
    }
    p = build_memfind(json, strlen(json), "\"file\":\"");
    if (p != NULL) {
        const char *q = p + 8;
        uint32_t w = 0;
        while (*q != 0 && *q != '"' && w + 1u < sizeof(info->file)) {
            info->file[w++] = *q++;
        }
        info->file[w] = 0;
    }
    p = build_memfind(json, strlen(json), "\"line\":");
    if (p != NULL) {
        info->line = (uint32_t)strtoul(p + 7, NULL, 10);
    }
}

/* notes from our own ledger (raw JSONL lines, target = fqn). */
static void at_fill_notes(pm_metal_build_at_info_t *info) {
    int32_t rc = pm_metal_build_notes_query(info->fqn, -1,
        info->notes, sizeof(info->notes), &info->n_notes);
    if (rc < 0) {
        info->n_notes = 0;
    }
}

/* kind tag from the registry's export kind (same mapping as inspect's
 * kind_tag). */
static const char *at_kind_tag(pm_wasmmod_registry_export_kind_t k) {
    switch (k) {
        case PM_WASMMOD_REGISTRY_EXPORT_FN:     return "fn";
        case PM_WASMMOD_REGISTRY_EXPORT_MEM:    return "mem";
        case PM_WASMMOD_REGISTRY_EXPORT_OBJ:    return "obj";
        case PM_WASMMOD_REGISTRY_EXPORT_I64:    return "i64";
        case PM_WASMMOD_REGISTRY_EXPORT_F32:    return "f32";
        case PM_WASMMOD_REGISTRY_EXPORT_F64:    return "f64";
        case PM_WASMMOD_REGISTRY_EXPORT_BUFPTR: return "bufptr";
        default:                                return "?";
    }
}

/* Resolve fqn (+ optional export name) against the live registry first,
 * then layer the build record, the doc, the notes, and the manifest deps.
 * Returns a handle for at_info / at_ast, NONE when fqn is unknown to both
 * the registry and the embedded source table. */
pm_metal_build_at_handle_t pm_metal_build_at(const char *fqn, const char *name) {
    pm_build_at_slot_t *slot;
    pm_metal_build_at_info_t *info;
    const pm_metal_src_card_t *card;
    const pm_metal_build_record_t *rec;
    uint8_t fqnb[PM_METAL_BUILD_AT_FQN_MAX];
    uint32_t flen;
    uint32_t i;

    if (fqn == NULL || fqn[0] == 0) {
        return PM_METAL_BUILD_AT_NONE;
    }
    flen = (uint32_t)strlen(fqn);
    if (flen >= sizeof(fqnb)) {
        return PM_METAL_BUILD_AT_NONE;
    }
    memcpy(fqnb, fqn, flen);

    /* resolve: the live registry first, then the embedded source table. A
     * fqn unknown to both is not resolvable. */
    card = pm_metal_src_find(fqn);
    if (pm_wasmmod_registry_has(fqnb, flen) != 1 && card == NULL) {
        return PM_METAL_BUILD_AT_NONE;
    }

    slot = &s_at[s_at_epoch % PM_METAL_BUILD_AT_SLOTS];
    s_at_epoch++;
    memset(slot, 0, sizeof(*slot));
    info = &slot->info;
    snprintf(info->fqn, sizeof(info->fqn), "%s", fqn);
    if (name != NULL && name[0] != 0) {
        snprintf(info->name, sizeof(info->name), "%s", name);
    } else {
        snprintf(info->name, sizeof(info->name), "%s", fqn);
        name = NULL;    /* card-level query */
    }
    info->lang[0] = 0;
    if (card != NULL && card->impl != NULL) {
        snprintf(info->lang, sizeof(info->lang), "%s", card->impl);
    }

    if (name != NULL) {
        /* face-level: find the export in the live registry */
        uint32_t nx = pm_wasmmod_registry_export_count(fqnb, flen);
        int found = 0;
        for (i = 0; i < nx; i++) {
            uint8_t ename[PM_METAL_BUILD_AT_NAME_MAX];
            uint32_t elen = sizeof(ename);
            uint8_t esig[PM_METAL_BUILD_AT_SIG_MAX];
            uint32_t slen = sizeof(esig);
            pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
            if (pm_wasmmod_registry_export_at(fqnb, flen, i, ename, &elen,
                    &kind, esig, &slen) != 0
                    && elen == (uint32_t)strlen(name)
                    && memcmp(ename, name, elen) == 0) {
                snprintf(info->kind, sizeof(info->kind), "%s",
                    at_kind_tag(kind));
                if (slen > 0 && slen < sizeof(esig)) {
                    memcpy(info->sig, esig, slen);
                    info->sig[slen] = 0;
                }
                found = 1;
                break;
            }
        }
        if (!found) {
            /* not a live export: an embedded-source face (documented but not
             * registered, or a card-local helper). kind stays empty — info
             * still resolves with doc/notes/deps from the source table. */
            info->kind[0] = 0;
        }
        at_fill_doc(info);
    } else {
        snprintf(info->kind, sizeof(info->kind), "mod");
        /* card-level: no doc face, but the manifest may carry one */
    }

    /* provenance: the build record (present when fqn was unit_compiled) */
    rec = pm_metal_build_record_find(fqn);
    if (rec != NULL) {
        info->has_record = 1;
        info->n_sources = rec->n_sources;
        info->n_syms = rec->n_syms;
    }

    at_fill_notes(info);
    at_fill_deps(info);
    slot->valid = 1;
    return (pm_metal_build_at_handle_t)((uintptr_t)(slot - s_at) + 1u);
}

int32_t pm_metal_build_at_info(pm_metal_build_at_handle_t handle,
    pm_metal_build_at_info_t *info) {
    pm_build_at_slot_t *slot;
    if (info == NULL || handle == PM_METAL_BUILD_AT_NONE
        || handle > PM_METAL_BUILD_AT_SLOTS) {
        return -1;
    }
    slot = &s_at[handle - 1u];
    if (!slot->valid) {
        return -1;
    }
    *info = slot->info;
    return 0;
}

int32_t pm_metal_build_at_ast(pm_metal_build_at_handle_t handle,
    char *lang_out, size_t lang_max) {
    pm_build_at_slot_t *slot;
    const char *lang;
    if (handle == PM_METAL_BUILD_AT_NONE || handle > PM_METAL_BUILD_AT_SLOTS) {
        return -1;
    }
    slot = &s_at[handle - 1u];
    if (!slot->valid) {
        return -1;
    }
    lang = slot->info.lang[0] != 0 ? slot->info.lang : "c";
    if (lang_out != NULL && lang_max > 0) {
        snprintf(lang_out, lang_max, "%s", lang);
    }
    /* Phase 12 fills the C leaf (TCC tree); Rust/C++ editors come later.
     * The spine itself is language-neutral: the dispatch is the contract. */
    if (strcmp(lang, "c") == 0) {
        return 1;
    }
    return 0;
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
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_note_add, pm_metal_build_note_add,
    int32_t(const char *, pm_metal_build_note_kind_t, const char *,
        const char *const *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_notes_query, pm_metal_build_notes_query,
    int32_t(const char *, int32_t, char *, size_t, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_note_has, pm_metal_build_note_has,
    int32_t(const char *, pm_metal_build_note_kind_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_ledger_path, pm_metal_build_ledger_path,
    const char *(void));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_destroy, pm_metal_build_artifact_destroy,
    void(pm_metal_build_artifact_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_lookup, pm_metal_build_artifact_lookup,
    void *(const pm_metal_build_artifact_t *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_call, pm_metal_build_artifact_call,
    int32_t(const pm_metal_build_artifact_t *, const char *, const int64_t *, uint32_t, int64_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_at, pm_metal_build_at,
    pm_metal_build_at_handle_t(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_at_info, pm_metal_build_at_info,
    int32_t(pm_metal_build_at_handle_t, pm_metal_build_at_info_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_at_ast, pm_metal_build_at_ast,
    int32_t(pm_metal_build_at_handle_t, char *, size_t));
