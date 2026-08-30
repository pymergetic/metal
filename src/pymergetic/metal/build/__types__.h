/* pymergetic.metal.build — parse __pmm__.toml manifests, resolve the
 * dependency graph, and (Phase 3) compile + link card sources in-process.
 *
 * The parser speaks exactly the Phase-1 external-manifest schema:
 *   key = "string" | 123 | ["a", "b"]   plus # comments and [table]
 *   headers (ignored — the manifests are flat).
 */
#ifndef PYMERGETIC_METAL_BUILD_TYPES_H
#define PYMERGETIC_METAL_BUILD_TYPES_H

#include "pymergetic/util/mem/__types__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_BUILD_ERR_MAX 160u
#define PM_METAL_BUILD_STR_MAX 128u
#define PM_METAL_BUILD_MAX_OBJS 8u

typedef struct pm_metal_build_unit {
    char fqn[PM_METAL_BUILD_STR_MAX];
    char impl[8];   /* "c" | "cpp" | "rs" */
    char version[PM_METAL_BUILD_STR_MAX];
    const char **sources;    /* arena-owned */
    uint32_t n_sources;
    const char **include_dirs;
    uint32_t n_include_dirs;
    const char **defines;
    uint32_t n_defines;
    const char **depends;    /* fqns this unit must be built after */
    uint32_t n_depends;
} pm_metal_build_unit_t;

typedef struct pm_metal_build_artifact {
    char fqn[PM_METAL_BUILD_STR_MAX];
    uint8_t *bytes;          /* arena-owned */
    size_t len;
    int32_t is_wasm;
    /* wasm-seat link: the loader handle of each loaded module (the registry
     * owns the published exports; destroy unloads the modules). Unused
     * (zeroed) on ELF seats. */
    pm_wasmmod_registry_handle_t loader_handles[PM_METAL_BUILD_MAX_OBJS];
    uint32_t n_loader_handles;
} pm_metal_build_artifact_t;

typedef enum pm_metal_build_status {
    PM_METAL_BUILD_OK = 0,
    PM_METAL_BUILD_ERR_PARSE = -1,
    PM_METAL_BUILD_ERR_CYCLE = -2,
    PM_METAL_BUILD_ERR_MISSING_DEP = -3,
    PM_METAL_BUILD_ERR_NOMEM = -4,
    PM_METAL_BUILD_ERR_COMPILE = -5,
    PM_METAL_BUILD_ERR_LINK = -6,
} pm_metal_build_status_t;

/* Parse one manifest into unit (arena-backed; strings are copied into the
 * arena). Returns PM_METAL_BUILD_OK or a negative status; errbuf carries the
 * parse error with a line number when non-NULL. */
int32_t pm_metal_build_unit_parse(pm_util_mem_arena_t *arena,
    const uint8_t *bytes, size_t len, pm_metal_build_unit_t *unit,
    char *errbuf, size_t errbuf_len);

/* Topologically order units on their depends edges (dependencies first).
 * order receives an arena-owned array of pointers into units. A cycle, a
 * depends-edge naming an unknown fqn, or a duplicate fqn is an error. */
int32_t pm_metal_build_graph_resolve(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t *units, uint32_t n_units,
    const pm_metal_build_unit_t ***order, uint32_t *n_order,
    char *errbuf, size_t errbuf_len);

/* Compile one source of unit into an object (arena-owned bytes in obj_out).
 * unit_root is the directory the unit's relative include_dirs resolve
 * against (the manifest's own directory); source is C source text.
 * Phase 3 drives the jit.c card's TCC object path. */
int32_t pm_metal_build_compile_source(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root, const char *source,
    uint8_t **obj_out, size_t *obj_len, char *errbuf, size_t errbuf_len);

/* compile_source with the cross-compile knob: target selects which TCC
 * backend makes the object. JIT_C_TARGET_SEAT is exactly compile_source;
 * JIT_C_TARGET_WASM32 cross-compiles a wasm module on ELF seats that link
 * the second (prefixed) wasm32 libtcc instance. The object format follows
 * the target — an ELF ET_REL for the seat, a serialized wasm module for
 * wasm32 — and the link face accepts either shape (ELF relocator on ELF
 * seats, loader publish on wasm-capable seats). */
int32_t pm_metal_build_compile_source_target(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root, const char *source,
    int32_t target,
    uint8_t **obj_out, size_t *obj_len, char *errbuf, size_t errbuf_len);

/* Link the unit's compiled objects through the in-tree ELF relocator and
 * return the loaded, executable image. artifact->bytes carries the image
 * pointer — release it with pm_metal_build_artifact_destroy. */
int32_t pm_metal_build_link(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, uint8_t **objects, const size_t *lens,
    uint32_t n_objects, pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len);

/* Release a link artifact (munmap the image). Safe on NULL / empty. */
void pm_metal_build_artifact_destroy(pm_metal_build_artifact_t *artifact);

/* Look up a function symbol in a linked artifact. Returns NULL when the
 * name is not present. */
void *pm_metal_build_artifact_lookup(const pm_metal_build_artifact_t *artifact,
    const char *name);

/* Call a function in a linked artifact with scalar (i64-transport) args.
 * Returns 0 on a completed call, negative on refusal. The result lands in
 * *res when res is non-NULL (0-2 args on ELF seats; i32 spine on the wasm
 * seat, widened to i64 on return). */
int32_t pm_metal_build_artifact_call(const pm_metal_build_artifact_t *artifact,
    const char *name, const int64_t *args, uint32_t n_args, int64_t *res);

/* Runtime card discovery: walk the embedded card source table (every seat
 * ships it — tools/embed_src.py), parse each card's raw __pmm__.toml with
 * pm_metal_build_unit_parse, and synthesize one unit per card. Units receive
 * an arena-owned array in *units; n_units is its length.
 *
 * impl="c" cards become buildable units: sources are the card's embedded
 * muscle file names, include_dirs/defines empty — the caller supplies the
 * seat's include roots + defines at compile time (the seat fill, like
 * io.fetch differs per seat). impl="rs"/"py" cards are listed too, with
 * impl copied verbatim: pm_metal_build_unit_compile refuses them with a
 * clear "not yet buildable" error rather than silently skipping. */
int32_t pm_metal_build_discover(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t **units, uint32_t *n_units,
    char *errbuf, size_t errbuf_len);

/* Compile every source of unit (via pm_metal_build_compile_source, rooted at
 * unit_root + include_dirs/extra_defines — the seat fill) and link the
 * objects through the in-tree ELF relocator with the process resolver.
 * One call: sources -> artifact. */
int32_t pm_metal_build_unit_compile(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **extra_defines, uint32_t n_extra_defines,
    pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len);

/*------------------ build records (provenance chain) ------------------
 * Every unit_compile retains a record: the unit's sources, the per-source
 * object bytes, the linked image's exported symbols. The inspector serves
 * these as /build/<fqn> — authored source stays the primary pane, the
 * record is the build-product pane with the provenance chain in between. */

#define PM_METAL_BUILD_MAX_RECORDS 8u
#define PM_METAL_BUILD_MAX_SRC_PATH 96u
#define PM_METAL_BUILD_MAX_SYMS 64u
#define PM_METAL_BUILD_SYM_NAME_MAX 64u

typedef struct pm_metal_build_record {
    char fqn[PM_METAL_BUILD_STR_MAX];
    int32_t valid;                       /* slot in use */
    /* per-source objects: path + the .o length it compiled to */
    char src_paths[PM_METAL_BUILD_MAX_OBJS][PM_METAL_BUILD_MAX_SRC_PATH];
    uint32_t obj_lens[PM_METAL_BUILD_MAX_OBJS];
    uint32_t n_sources;
    /* the linked image's exported function symbols (name only — addresses
     * are seat-local and not stable across runs) */
    const char *sym_names[PM_METAL_BUILD_MAX_SYMS];
    char sym_names_buf[PM_METAL_BUILD_MAX_SYMS][PM_METAL_BUILD_SYM_NAME_MAX];
    uint32_t n_syms;
} pm_metal_build_record_t;

/* The record of the most recent unit_compile of fqn (NULL when never built
 * or after record_reset). The record table is retained until reset — build
 * products stay inspectable as long as the images are live. */
const pm_metal_build_record_t *pm_metal_build_record_find(const char *fqn);

/* Drop every retained record (called by the record owner at teardown; the
 * linked images themselves are freed via artifact_destroy). */
void pm_metal_build_record_reset(void);

/*------------------ change ledger (fs-backed, JSON-lines) ------------------
 * Every source mutation carries a note: what kind of change, why, and the
 * things it touches. The ledger is one card-owned file in the fs card
 * (/src/.changes.jsonl) — one JSON object per line, appended by the build
 * card (it owns write-back) and queried by anyone. Phase 11's AST editor
 * refuses a write-back without a matching note for the target. */

#define PM_METAL_BUILD_NOTE_TARGET_MAX 160u
#define PM_METAL_BUILD_NOTE_REASON_MAX 256u
#define PM_METAL_BUILD_NOTE_REFS_MAX 8u
#define PM_METAL_BUILD_LEDGER_MAX (64u * 1024u)

typedef enum pm_metal_build_note_kind {
    PM_METAL_BUILD_NOTE_CHANGE = 0,
    PM_METAL_BUILD_NOTE_DECISION = 1,
    PM_METAL_BUILD_NOTE_WARNING = 2,
    PM_METAL_BUILD_NOTE_TODO = 3,
} pm_metal_build_note_kind_t;

/* Append one note as a JSON line to /src/.changes.jsonl. kind is validated;
 * an empty reason is refused (a note without a reason is noise). refs are
 * optional targets the note touches (card fqns, file paths). Returns 0 on
 * append, a negative status on refusal. */
int32_t pm_metal_build_note_add(const char *target,
    pm_metal_build_note_kind_t kind, const char *reason,
    const char *const *refs, uint32_t n_refs);

/* Query notes: lines whose target matches (exact) or all when target is
 * NULL, filtered by kind when kind >= 0. Matching lines are concatenated
 * (newline-joined) into out, which receives the byte count. Returns the
 * number of matching lines, or a negative status on error. */
int32_t pm_metal_build_notes_query(const char *target,
    int32_t kind, char *out, size_t out_len, uint32_t *out_n);

/* True when target has at least one note of kind (the write-back gate:
 * Phase 11's editor refuses a mutation without this). */
int32_t pm_metal_build_note_has(const char *target,
    pm_metal_build_note_kind_t kind);

/* The ledger path in the fs card — the one file the build card owns. */
const char *pm_metal_build_ledger_path(void);

/*------------------ accessor spine (Phase 11) ------------------
 * One language-neutral query shape over everything the earlier phases
 * built: the live registry (Phase 4/8: what actually runs + provenance),
 * the doc extractor (Phase 9), the change ledger (Phase 10), and the
 * embedded source table. `b.at(fqn, name)` resolves; at_info carries the
 * joined answer; at_ast dispatches to the per-language editor (Phase 12).
 *
 * at() prefers the live registry — what actually executes — and layers the
 * build record on top for provenance: the same query before and after a
 * runtime rebuild returns the same identity pointed at the new record. */

typedef uint32_t pm_metal_build_at_handle_t;
#define PM_METAL_BUILD_AT_NONE 0u

#define PM_METAL_BUILD_AT_FQN_MAX 128u
#define PM_METAL_BUILD_AT_NAME_MAX 128u
#define PM_METAL_BUILD_AT_SIG_MAX 192u
#define PM_METAL_BUILD_AT_DOC_MAX 512u
#define PM_METAL_BUILD_AT_NOTES_MAX 512u
#define PM_METAL_BUILD_AT_REFS_MAX 12u
#define PM_METAL_BUILD_AT_REF_MAX 128u
#define PM_METAL_BUILD_AT_FILE_MAX 96u

typedef struct pm_metal_build_at_info {
    /* identity */
    char fqn[PM_METAL_BUILD_AT_FQN_MAX];
    char name[PM_METAL_BUILD_AT_NAME_MAX];
    char kind[8];       /* "fn" | "mem" | "obj" | "i64" | "f32" | "f64" | "mod" */
    char lang[8];       /* card impl: "c" | "rs" | "py" */
    /* registry face */
    char sig[PM_METAL_BUILD_AT_SIG_MAX];    /* empty when the face is not an fn */
    /* provenance (build record; present when fqn was unit_compiled) */
    int32_t has_record;
    uint32_t n_sources;
    uint32_t n_syms;
    /* Phase 9: doc prose first-line + the file/line the extractor found */
    char doc[PM_METAL_BUILD_AT_DOC_MAX];    /* empty when undocumented */
    char file[PM_METAL_BUILD_AT_FILE_MAX];  /* embedded-source relative path */
    uint32_t line;
    /* Phase 10: the ledger notes for this target (raw JSONL lines) */
    char notes[PM_METAL_BUILD_AT_NOTES_MAX];
    uint32_t n_notes;
    /* call-graph: fqns this face's card imports from (connect_import edges) */
    char deps[PM_METAL_BUILD_AT_REFS_MAX][PM_METAL_BUILD_AT_REF_MAX];
    uint32_t n_deps;
} pm_metal_build_at_info_t;

/* Resolve fqn (+ optional export name; NULL = the card itself, kind "mod")
 * against the live registry, then layer the build record + doc + notes +
 * deps. Returns a handle for at_info / at_ast, PM_METAL_BUILD_AT_NONE when
 * fqn is unknown to both the registry and the embedded source table. */
pm_metal_build_at_handle_t pm_metal_build_at(const char *fqn, const char *name);

/* Fill info from a handle returned by at(). Returns 0 on success, -1 when
 * the handle is stale (record_reset between at() and at_info()). */
int32_t pm_metal_build_at_info(pm_metal_build_at_handle_t handle,
    pm_metal_build_at_info_t *info);

/* The per-language editor leaf. Tonight's spine returns the language and a
 * flag saying whether an editor exists for it (Phase 12 fills the C leaf;
 * Rust/C++ later). lang_out receives "c" | "rs" | "cpp" | "py". Returns 1
 * when an editor exists, 0 when the language has none yet, -1 on a bad
 * handle. */
int32_t pm_metal_build_at_ast(pm_metal_build_at_handle_t handle,
    char *lang_out, size_t lang_max);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BUILD_TYPES_H */
