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
 * Phase 3 drives the jit.c card's TCC object path. */
int32_t pm_metal_build_compile_source(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *source,
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

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BUILD_TYPES_H */
