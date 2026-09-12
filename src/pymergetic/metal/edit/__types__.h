/* pymergetic.metal.edit — the language editor card (Phase 12).
 *
 * The C editor is span-addressed, not a tree re-printer: locate the byte
 * span of a top-level construct (function definition, #define) and splice
 * the edit, leaving every byte outside the span untouched. Formatting,
 * comments, and unrelated code are preserved by construction — the only
 * bytes that change are the ones the edit names.
 *
 * libtcc exposes no AST (its tree is internal), so this is the honest AST
 * libtcc can support: spans validated by a real TCC parse. The typecheck
 * gate runs TCC over the edited source before any write — an edit that
 * breaks compilation errors at edit time, never on the next build.
 *
 * write_back is note-enforced (Phase 10): a mutation without a matching
 * ledger note for the target is refused. The editor flow:
 *   parse -> locate -> mutate -> typecheck -> note_add -> write_back
 * with note_add before write_back, both required, both enforced.
 */
#ifndef PYMERGETIC_METAL_EDIT_TYPES_H
#define PYMERGETIC_METAL_EDIT_TYPES_H

#include "pymergetic/util/mem/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_EDIT_ERR_MAX 256u
#define PM_METAL_EDIT_NAME_MAX 96u
/* Where a parse starts, not where it stops. Each of these is the default of a
 * knob on pymergetic.util.limits (node, source, typecheck):
 * the node list is arena memory that grows with the file, the source bound is
 * what the editor will accept rather than what it holds, and the typecheck
 * scratch is taken for one compile and given straight back. A seat editing
 * something bigger than the defaults moves the knob; nothing here is standing
 * memory waiting for an edit that may never come. */
#define PM_METAL_EDIT_NODES_DEFAULT 256u
#define PM_METAL_EDIT_SRC_DEFAULT (128u * 1024u)
#if defined(PM_METAL_FIRMWARE)
/* The tccpp pools are 2 x 256KB and a typecheck source is small; a board's
 * whole free map may be only a few megabytes, so this seat starts tight. */
#define PM_METAL_EDIT_TYPECHECK_DEFAULT (2u * 1024u * 1024u)
#else
#define PM_METAL_EDIT_TYPECHECK_DEFAULT (8u * 1024u * 1024u)
#endif

typedef enum pm_metal_edit_kind {
    PM_METAL_EDIT_NONE = 0,
    PM_METAL_EDIT_FN = 1,       /* top-level function definition */
    PM_METAL_EDIT_DEFINE = 2,   /* #define NAME value (function-like too) */
} pm_metal_edit_kind_t;

typedef struct pm_metal_edit_node {
    pm_metal_edit_kind_t kind;
    char name[PM_METAL_EDIT_NAME_MAX];
    uint32_t name_off;      /* offset of the identifier in the source */
    uint32_t span_start;    /* first byte of the construct */
    uint32_t span_end;      /* one past the last byte ('}' or line end) */
    uint32_t body_off;      /* FN: offset of the '{'; DEFINE: value start */
    uint32_t value_off;     /* DEFINE: first byte of the value token */
    uint32_t value_len;     /* DEFINE: length of the value token */
    uint32_t line;          /* 1-based line of span_start */
} pm_metal_edit_node_t;

typedef struct pm_metal_edit_tree {
    /* the source the spans index (borrowed, not copied — the tree is only
     * valid while the source outlives it) */
    const char *src;
    size_t src_len;
    uint32_t n_nodes;
    /* arena-owned, grown under the node knob while the file needs it, handed
     * back by pm_metal_edit_tree_release */
    uint32_t cap_nodes;
    pm_metal_edit_node_t *nodes;
    char error[PM_METAL_EDIT_ERR_MAX];
} pm_metal_edit_tree_t;

typedef enum pm_metal_edit_status {
    PM_METAL_EDIT_OK = 0,
    PM_METAL_EDIT_ERR_ARGS = -1,
    PM_METAL_EDIT_ERR_NOT_FOUND = -2,
    PM_METAL_EDIT_ERR_TYPECHECK = -3,   /* edited source no longer compiles */
    PM_METAL_EDIT_ERR_NOTE = -4,        /* write-back without a ledger note */
    PM_METAL_EDIT_ERR_NOMEM = -5,
    PM_METAL_EDIT_ERR_IO = -6,
} pm_metal_edit_status_t;

/* Span-parse C source into a tree of top-level constructs: function
 * definitions and #defines. The parse is lexical (identifier at depth 0
 * followed by '(' then a matching '{' at column 0, or a #define line) and
 * validated by a real TCC compile before write_back — a construct the
 * span-parse mislocates surfaces as a typecheck failure there, never a
 * silent mis-edit. Returns PM_METAL_EDIT_OK or a negative status. */
int32_t pm_metal_edit_parse_c(pm_util_mem_arena_t *arena,
    pm_metal_edit_tree_t *tree, const char *source, size_t source_len);

/* Give a parsed tree's nodes back to the arena they came from. The tree is
 * the caller's variable either way; this is the memory behind it. */
void pm_metal_edit_tree_release(pm_util_mem_arena_t *arena,
    pm_metal_edit_tree_t *tree);

/* Find a node by kind + name. Returns the node or NULL. */
const pm_metal_edit_node_t *pm_metal_edit_locate(
    const pm_metal_edit_tree_t *tree, pm_metal_edit_kind_t kind,
    const char *name);

/* Rewrite a #define NAME <old> to NAME <new>. The splice touches only the
 * value bytes: out receives the full edited source (arena-owned). Returns
 * PM_METAL_EDIT_OK or a negative status; NOT_FOUND when the define is
 * absent, ARGS when it has no value token. */
int32_t pm_metal_edit_set_define(pm_util_mem_arena_t *arena,
    const pm_metal_edit_tree_t *tree, const char *name,
    const char *value, char **out, size_t *out_len);

/* Replace the body of function name with body_text (a braced block or any
 * statement sequence — the splice inserts it between the header's '{' and
 * the matching '}'). Formatting outside the span is untouched. */
int32_t pm_metal_edit_set_fn_body(pm_util_mem_arena_t *arena,
    const pm_metal_edit_tree_t *tree, const char *name,
    const char *body_text, char **out, size_t *out_len);

/* The typecheck gate: compile source through the jit.c card's TCC object
 * path. Returns PM_METAL_EDIT_OK when it compiles, TYPECHECK with errbuf
 * carrying the compile error otherwise. */
int32_t pm_metal_edit_typecheck_c(const char *source, size_t source_len,
    char *errbuf, size_t errbuf_len);

/* Write the edited source back: fs-card write (drop + add at path),
 * gated on (a) a ledger note for target existing (note_has CHANGE) and
 * (b) the source passing the typecheck gate. The note is the author's
 * record made before the edit — write_back refuses without it (ERR_NOTE)
 * and refuses a broken edit (TYPECHECK). */
int32_t pm_metal_edit_write_back(const char *target, const char *path,
    const char *source, size_t source_len,
    char *errbuf, size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_EDIT_TYPES_H */
