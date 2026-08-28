#include "pymergetic/metal/edit/__exports__.h"

#include "pymergetic/metal/edit/__types__.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/fs/__exports__.h"
#include "pymergetic/util/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------ span parse ------------------
 * Two top-level shapes are located, the only two the editor edits:
 *
 *   FN:     a line starting at column 0 with an identifier, whose '(' and
 *           matching ')' are followed (whitespace aside) by '{', with the
 *           closing '}' also at column 0. Card __impl__.c style.
 *   DEFINE: a line '#define NAME ...' — NAME and (when present) the single
 *           value token after it. Function-like macros carry parens glued
 *           to the name; the editor locates them but only simple object-like
 *           defines carry a value span for set_define.
 *
 * Everything else (comments, includes, other directives, statics) is span
 * noise the parse skips: the editor never edits what it cannot address. */

static uint32_t line_of(const char *s, uint32_t off) {
    uint32_t line = 1;
    uint32_t i;
    for (i = 0; i < off && s[i] != 0; i++) {
        if (s[i] == '\n') {
            line++;
        }
    }
    return line;
}

static int is_id_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_id_char(char c) {
    return is_id_start(c) || (c >= '0' && c <= '9');
}

/* Find the matching close paren for the open paren at off. Returns the
 * offset of ')' or (uint32_t)-1. Nesting-aware; string/char literals and
 * comments skipped so a paren inside them cannot unbalance the count. */
static uint32_t match_paren(const char *s, size_t len, uint32_t off) {
    uint32_t depth = 0;
    uint32_t i = off;
    while (i < len) {
        char c = s[i];
        if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
            if (depth == 0) {
                return i;
            }
        } else if (c == '"' || c == '\'') {
            char q = c;
            i++;
            while (i < len && s[i] != q) {
                if (s[i] == '\\' && i + 1 < len) {
                    i++;
                }
                i++;
            }
        } else if (c == '/' && i + 1 < len && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/')) {
                i++;
            }
            i++;
        }
        i++;
    }
    return (uint32_t)-1;
}

int32_t pm_metal_edit_parse_c(pm_metal_edit_tree_t *tree,
    const char *source, size_t source_len) {
    size_t len;
    uint32_t i = 0;
    if (tree == NULL || source == NULL) {
        return PM_METAL_EDIT_ERR_ARGS;
    }
    memset(tree, 0, sizeof(*tree));
    len = source_len != 0 ? source_len : strlen(source);
    if (len == 0 || len > PM_METAL_EDIT_SRC_MAX) {
        snprintf(tree->error, sizeof(tree->error),
            "parse_c: source empty or too large (%zu)", len);
        return PM_METAL_EDIT_ERR_ARGS;
    }
    tree->src = source;
    tree->src_len = len;

    while (i < len) {
        uint32_t ls = i;    /* line start */
        uint32_t le = ls;   /* line end (exclusive) */
        while (le < len && source[le] != '\n') {
            le++;
        }

        /* #define NAME ... */
        if (le - ls >= 8u && memcmp(source + ls, "#define", 7) == 0
            && (source[ls + 7] == ' ' || source[ls + 7] == '\t')) {
            uint32_t p = ls + 7;
            while (p < le && (source[p] == ' ' || source[p] == '\t')) {
                p++;
            }
            if (p < le && is_id_start(source[p])) {
                uint32_t ns = p;
                uint32_t ne = p;
                pm_metal_edit_node_t *n;
                while (ne < le && is_id_char(source[ne])) {
                    ne++;
                }
                if (tree->n_nodes >= PM_METAL_EDIT_NODES_MAX) {
                    snprintf(tree->error, sizeof(tree->error),
                        "parse_c: too many nodes (max %u)",
                        (unsigned)PM_METAL_EDIT_NODES_MAX);
                    return PM_METAL_EDIT_ERR_NOMEM;
                }
                n = &tree->nodes[tree->n_nodes++];
                n->kind = PM_METAL_EDIT_DEFINE;
                snprintf(n->name, sizeof(n->name), "%.*s",
                    (int)(ne - ns), source + ns);
                n->name_off = ns;
                n->span_start = ls;
                n->span_end = le < len ? le + 1 : le;
                n->line = line_of(source, ls);
                /* value token: first non-space run after the name (or the
                 * glued '(...)' of a function-like macro). */
                n->body_off = ne;
                if (ne < le && source[ne] == '(') {
                    n->body_off = match_paren(source, len, ne) + 1u;
                }
                p = n->body_off;
                while (p < le && (source[p] == ' ' || source[p] == '\t')) {
                    p++;
                }
                n->value_off = p;
                n->value_len = 0;
                while (p + n->value_len < le
                    && source[p + n->value_len] != ' '
                    && source[p + n->value_len] != '\t') {
                    n->value_len++;
                }
            }
            i = le < len ? le + 1 : le;
            continue;
        }

        /* FN: a column-0 line that opens a top-level function definition.
         * The name is the identifier directly preceding the parameter '(' —
         * after any return type / storage-class tokens, so both
         * `int32_t name(...)` and `static uint32_t name(...)` resolve. The
         * header scan stops at the first ';', '=', '{', '}' or literal, so a
         * declaration, an initializer, or a typedef never poses as a fn. */
        if (is_id_start(source[ls]) && ls < le) {
            uint32_t p = ls;
            uint32_t id_start = 0;
            uint32_t id_end = 0;
            uint32_t paren_off = 0;
            int have_paren = 0;
            int stop = 0;
            while (p < len && !stop) {
                char c = source[p];
                if (is_id_start(c)) {
                    id_start = p;
                    while (p < len && is_id_char(source[p])) {
                        p++;
                    }
                    id_end = p;
                    continue;
                }
                if (c == '(') {
                    paren_off = p;
                    have_paren = 1;
                    stop = 1;
                    break;
                }
                if (c == ';' || c == '=' || c == '{' || c == '}'
                    || c == '"' || c == '\'') {
                    stop = 1;
                    break;
                }
                if (c == '/' && p + 1 < len && source[p + 1] == '/') {
                    stop = 1;
                    break;
                }
                if (c == '/' && p + 1 < len && source[p + 1] == '*') {
                    p += 2;
                    while (p + 1 < len
                        && !(source[p] == '*' && source[p + 1] == '/')) {
                        p++;
                    }
                    p++;
                    continue;
                }
                if (c == '\n') {
                    stop = 1;
                    break;
                }
                p++;
            }
            if (have_paren && id_end > id_start) {
                /* only whitespace between the name and '(' */
                uint32_t back = paren_off;
                while (back > id_end
                    && (source[back - 1] == ' ' || source[back - 1] == '\t')) {
                    back--;
                }
                if (back == id_end) {
                    uint32_t pe = match_paren(source, len, paren_off);
                    if (pe != (uint32_t)-1) {
                        uint32_t b = pe + 1;
                        while (b < len && (source[b] == ' '
                            || source[b] == '\t' || source[b] == '\r'
                            || source[b] == '\n')) {
                            b++;
                        }
                        if (b < len && source[b] == '{') {
                            uint32_t q = b + 1;
                            uint32_t depth = 1;
                            while (q < len && depth > 0) {
                                char c = source[q];
                                if (c == '{') {
                                    depth++;
                                } else if (c == '}') {
                                    depth--;
                                } else if (c == '"' || c == '\'') {
                                    char qc = c;
                                    q++;
                                    while (q < len && source[q] != qc) {
                                        if (source[q] == '\\'
                                            && q + 1 < len) {
                                            q++;
                                        }
                                        q++;
                                    }
                                } else if (c == '/' && q + 1 < len
                                    && source[q + 1] == '*') {
                                    q += 2;
                                    while (q + 1 < len
                                        && !(source[q] == '*'
                                            && source[q + 1] == '/')) {
                                        q++;
                                    }
                                    q++;
                                } else if (c == '/' && q + 1 < len
                                    && source[q + 1] == '/') {
                                    while (q < len && source[q] != '\n') {
                                        q++;
                                    }
                                }
                                q++;
                            }
                            if (depth == 0) {
                                pm_metal_edit_node_t *n;
                                if (tree->n_nodes
                                    >= PM_METAL_EDIT_NODES_MAX) {
                                    snprintf(tree->error,
                                        sizeof(tree->error),
                                        "parse_c: too many nodes (max %u)",
                                        (unsigned)PM_METAL_EDIT_NODES_MAX);
                                    return PM_METAL_EDIT_ERR_NOMEM;
                                }
                                n = &tree->nodes[tree->n_nodes++];
                                n->kind = PM_METAL_EDIT_FN;
                                snprintf(n->name, sizeof(n->name), "%.*s",
                                    (int)(id_end - id_start),
                                    source + id_start);
                                n->name_off = id_start;
                                n->span_start = ls;
                                n->span_end = q;    /* one past '}' */
                                n->body_off = b;    /* the '{' */
                                n->line = line_of(source, ls);
                                /* the body is consumed: an inner col-0 line
                                 * is span noise, not a second definition */
                                i = q < len ? q : len;
                                continue;
                            }
                        }
                    }
                }
            }
            i = le < len ? le + 1 : le;
            continue;
        }

        i = le < len ? le + 1 : le;
    }
    return PM_METAL_EDIT_OK;
}

const pm_metal_edit_node_t *pm_metal_edit_locate(
    const pm_metal_edit_tree_t *tree, pm_metal_edit_kind_t kind,
    const char *name) {
    uint32_t i;
    if (tree == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < tree->n_nodes; i++) {
        if (tree->nodes[i].kind == kind
            && strcmp(tree->nodes[i].name, name) == 0) {
            return &tree->nodes[i];
        }
    }
    return NULL;
}

/* splice: out = source[0..at) ++ new ++ source[at+len..source_len) */
static int32_t splice(pm_util_mem_arena_t *arena, const char *source,
    size_t source_len, uint32_t at, uint32_t remove_len,
    const char *insert, char **out, size_t *out_len) {
    size_t ilen = insert != NULL ? strlen(insert) : 0;
    size_t total;
    char *buf;
    if (at > source_len || (size_t)at + remove_len > source_len) {
        return PM_METAL_EDIT_ERR_ARGS;
    }
    total = source_len - remove_len + ilen;
    if (total >= PM_METAL_EDIT_SRC_MAX) {
        return PM_METAL_EDIT_ERR_NOMEM;
    }
    buf = (char *)pm_util_mem_alloc(arena, total + 1u);
    if (buf == NULL) {
        return PM_METAL_EDIT_ERR_NOMEM;
    }
    memcpy(buf, source, at);
    if (ilen > 0) {
        memcpy(buf + at, insert, ilen);
    }
    memcpy(buf + at + ilen, source + at + remove_len,
        source_len - at - remove_len);
    buf[total] = 0;
    *out = buf;
    *out_len = total;
    return PM_METAL_EDIT_OK;
}

int32_t pm_metal_edit_set_define(pm_util_mem_arena_t *arena,
    const pm_metal_edit_tree_t *tree, const char *name,
    const char *value, char **out, size_t *out_len) {
    const pm_metal_edit_node_t *n;
    if (arena == NULL || tree == NULL || name == NULL || value == NULL
        || out == NULL || out_len == NULL) {
        return PM_METAL_EDIT_ERR_ARGS;
    }
    n = pm_metal_edit_locate(tree, PM_METAL_EDIT_DEFINE, name);
    if (n == NULL) {
        return PM_METAL_EDIT_ERR_NOT_FOUND;
    }
    if (n->value_len == 0) {
        /* a function-like macro or a define with no value token: the
         * editor does not guess where a value would go */
        return PM_METAL_EDIT_ERR_ARGS;
    }
    return splice(arena, tree->src, tree->src_len,
        n->value_off, n->value_len, value, out, out_len);
}

int32_t pm_metal_edit_set_fn_body(pm_util_mem_arena_t *arena,
    const pm_metal_edit_tree_t *tree, const char *name,
    const char *body_text, char **out, size_t *out_len) {
    const pm_metal_edit_node_t *n;
    const char *src;
    size_t slen;
    uint32_t open;
    uint32_t close;
    uint32_t inner_start;
    uint32_t inner_len;
    if (arena == NULL || tree == NULL || name == NULL || body_text == NULL
        || out == NULL || out_len == NULL) {
        return PM_METAL_EDIT_ERR_ARGS;
    }
    n = pm_metal_edit_locate(tree, PM_METAL_EDIT_FN, name);
    if (n == NULL) {
        return PM_METAL_EDIT_ERR_NOT_FOUND;
    }
    src = tree->src;
    slen = tree->src_len;
    open = n->body_off;
    if (open >= slen || src[open] != '{') {
        return PM_METAL_EDIT_ERR_ARGS;
    }
    close = n->span_end - 1u;
    if (close >= slen || src[close] != '}') {
        return PM_METAL_EDIT_ERR_ARGS;
    }
    inner_start = open + 1u;
    inner_len = close - inner_start;
    return splice(arena, src, slen, inner_start, inner_len, body_text,
        out, out_len);
}

int32_t pm_metal_edit_typecheck_c(const char *source, size_t source_len,
    char *errbuf, size_t errbuf_len) {
    /* static backing: the arena only carries the compiled object, and one
     * typecheck runs at a time (the editor flow is sequential by contract).
     * Firmware seats link this card and have no malloc. */
    static uint8_t backing[256u * 1024u];
    pm_util_mem_arena_t *arena;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    int32_t rc;
    if (source == NULL || source_len == 0) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "typecheck: empty source");
        }
        return PM_METAL_EDIT_ERR_ARGS;
    }
    arena = pm_util_mem_arena_create(backing, sizeof(backing));
    if (arena == NULL) {
        return PM_METAL_EDIT_ERR_NOMEM;
    }
    rc = pm_metal_jit_c_object_compile(arena, source, source_len,
        &obj, &obj_len, errbuf, errbuf_len);
    pm_util_mem_arena_destroy(arena);
    if (rc != 0) {
        return PM_METAL_EDIT_ERR_TYPECHECK;
    }
    return PM_METAL_EDIT_OK;
}

int32_t pm_metal_edit_write_back(const char *target, const char *path,
    const char *source, size_t source_len,
    char *errbuf, size_t errbuf_len) {
    int32_t rc;
    if (target == NULL || path == NULL || source == NULL || source_len == 0) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "write_back: bad args");
        }
        return PM_METAL_EDIT_ERR_ARGS;
    }
    /* (a) the note gate: no ledger note, no write — the Phase 10 contract */
    if (pm_metal_build_note_has(target, PM_METAL_BUILD_NOTE_CHANGE) != 1) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len,
                "write_back: no ledger note for %s — note_add before write",
                target);
        }
        return PM_METAL_EDIT_ERR_NOTE;
    }
    /* (b) the typecheck gate: the edit must still compile */
    rc = pm_metal_edit_typecheck_c(source, source_len, errbuf, errbuf_len);
    if (rc != PM_METAL_EDIT_OK) {
        return rc;
    }
    /* (c) the write: fs drop + add (add refuses duplicates) */
    pm_metal_fs_drop(path);
    rc = pm_metal_fs_add(path, (const uint8_t *)source,
        (uint32_t)source_len);
    if (rc < 0) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "write_back: fs add failed");
        }
        return PM_METAL_EDIT_ERR_IO;
    }
    return PM_METAL_EDIT_OK;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.edit, pm_metal_edit_parse_c, pm_metal_edit_parse_c,
    int32_t(pm_metal_edit_tree_t *, const char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.edit, pm_metal_edit_locate, pm_metal_edit_locate,
    const pm_metal_edit_node_t *(const pm_metal_edit_tree_t *,
        pm_metal_edit_kind_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.edit, pm_metal_edit_set_define, pm_metal_edit_set_define,
    int32_t(pm_util_mem_arena_t *, const pm_metal_edit_tree_t *, const char *,
        const char *, char **, size_t *));
PM_MOD_EXPORT_C(pymergetic.metal.edit, pm_metal_edit_set_fn_body, pm_metal_edit_set_fn_body,
    int32_t(pm_util_mem_arena_t *, const pm_metal_edit_tree_t *, const char *,
        const char *, char **, size_t *));
PM_MOD_EXPORT_C(pymergetic.metal.edit, pm_metal_edit_typecheck_c, pm_metal_edit_typecheck_c,
    int32_t(const char *, size_t, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.edit, pm_metal_edit_write_back, pm_metal_edit_write_back,
    int32_t(const char *, const char *, const char *, size_t,
        char *, size_t));
