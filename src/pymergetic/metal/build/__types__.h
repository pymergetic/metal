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

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_BUILD_ERR_MAX 160u
#define PM_METAL_BUILD_STR_MAX 128u

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
#define PM_METAL_BUILD_MAX_OBJS 8u
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

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BUILD_TYPES_H */
