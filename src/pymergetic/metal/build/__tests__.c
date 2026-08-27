/* pymergetic.metal.build tests:
 *  - parse the REAL externals/tcc/__pmm__.toml (found relative to __FILE__,
 *    never the process cwd) and assert fqn/impl/defines + every source exists
 *  - topological order of a synthetic 3-unit graph with a dependency edge
 *  - a cyclic synthetic graph must error
 */
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1u);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    buf[n] = '\0';
    *out_len = (size_t)n;
    return buf;
}

/* externals/tcc/__pmm__.toml relative to this file: src/pymergetic/metal/build/ */
#define TCC_MANIFEST_REL "../../../../externals/tcc/__pmm__.toml"

static int32_t test_parse_real_tcc_manifest(void) {
    char path[512];
    size_t len = 0;
    char *bytes;
    void *backing;
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t unit;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;
    uint32_t i;

    snprintf(path, sizeof(path), "%s", __FILE__);
    /* strip the trailing filename: __FILE__ is the test's own .c path */
    char *slash = strrchr(path, '/');
    if (!slash) return 1;
    *slash = '\0';
    snprintf(path + strlen(path), sizeof(path) - strlen(path),
        "/" TCC_MANIFEST_REL);

    bytes = read_file(path, &len);
    if (!bytes) return 2;

    backing = malloc(1u << 18);
    if (!backing) { free(bytes); return 3; }
    arena = pm_util_mem_arena_create(backing, 1u << 18);
    if (!arena) { free(backing); free(bytes); return 4; }

    rc = pm_metal_build_unit_parse(arena, (const uint8_t *)bytes, len, &unit,
        err, sizeof(err));
    free(bytes);
    if (rc != PM_METAL_BUILD_OK) { pm_util_mem_arena_destroy(arena); free(backing); return 5; }

    if (strcmp(unit.fqn, "pymergetic.metal.external.tcc") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 6;
    }
    if (strcmp(unit.impl, "c") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 7;
    }
    if (strcmp(unit.version, "0.9.28rc") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 8;
    }
    if (unit.n_defines != 1 || strcmp(unit.defines[0], "TCC_TARGET_X86_64") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 9;
    }
    if (unit.n_include_dirs != 1 || strcmp(unit.include_dirs[0], ".") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 10;
    }
    if (unit.n_sources < 10) {
        pm_util_mem_arena_destroy(arena); free(backing); return 11;
    }
    /* every listed source must exist on disk next to the manifest */
    for (i = 0; i < unit.n_sources; i++) {
        char src_path[512];
        FILE *f;
        char *s2;
        snprintf(src_path, sizeof(src_path), "%s", path);
        s2 = strrchr(src_path, '/');
        if (!s2) { pm_util_mem_arena_destroy(arena); free(backing); return 12; }
        *s2 = '\0';
        snprintf(src_path + strlen(src_path), sizeof(src_path) - strlen(src_path),
            "/%s", unit.sources[i]);
        f = fopen(src_path, "rb");
        if (!f) {
            pm_util_mem_arena_destroy(arena); free(backing);
            return (int32_t)(20 + i);
        }
        fclose(f);
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static const char *S_MANIFEST_A =
    "fqn = \"test.a\"\n"
    "impl = \"c\"\n"
    "version = \"1.0\"\n"
    "sources = [\"a.c\"]\n"
    "depends = [\"test.b\"]\n";
static const char *S_MANIFEST_B =
    "fqn = \"test.b\"\n"
    "impl = \"c\"\n"
    "sources = [\"b.c\"]\n"
    "depends = []\n";
static const char *S_MANIFEST_C =
    "fqn = \"test.c\"\n"
    "impl = \"rs\"\n"
    "sources = [\"c.rs\"]\n"
    "depends = [\"test.a\"]\n";
static const char *S_MANIFEST_CYCLE_X =
    "fqn = \"cyc.x\"\n"
    "impl = \"c\"\n"
    "depends = [\"cyc.y\"]\n";
static const char *S_MANIFEST_CYCLE_Y =
    "fqn = \"cyc.y\"\n"
    "impl = \"c\"\n"
    "depends = [\"cyc.x\"]\n";

static int32_t parse_into(pm_util_mem_arena_t *arena, const char *src,
    pm_metal_build_unit_t *u) {
    char err[PM_METAL_BUILD_ERR_MAX];
    return pm_metal_build_unit_parse(arena, (const uint8_t *)src, strlen(src), u,
        err, sizeof(err));
}

static int32_t test_graph_order(void) {
    void *backing = malloc(1u << 16);
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t units[3];
    const pm_metal_build_unit_t **order = NULL;
    uint32_t n_order = 0;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;

    if (!backing) return 30;
    arena = pm_util_mem_arena_create(backing, 1u << 16);
    if (!arena) { free(backing); return 31; }

    /* pass them in deliberately worst-first order: c -> a -> b */
    if (parse_into(arena, S_MANIFEST_C, &units[0]) != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 32;
    }
    if (parse_into(arena, S_MANIFEST_A, &units[1]) != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 33;
    }
    if (parse_into(arena, S_MANIFEST_B, &units[2]) != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 34;
    }

    rc = pm_metal_build_graph_resolve(arena, units, 3, &order, &n_order, err,
        sizeof(err));
    if (rc != PM_METAL_BUILD_OK) { pm_util_mem_arena_destroy(arena); free(backing); return 35; }
    if (n_order != 3) { pm_util_mem_arena_destroy(arena); free(backing); return 36; }

    /* b before a (a depends on b), a before c (c depends on a) */
    {
        int bi = -1, ai = -1, ci = -1;
        uint32_t i;
        for (i = 0; i < n_order; i++) {
            if (strcmp(order[i]->fqn, "test.b") == 0) bi = (int)i;
            if (strcmp(order[i]->fqn, "test.a") == 0) ai = (int)i;
            if (strcmp(order[i]->fqn, "test.c") == 0) ci = (int)i;
        }
        if (bi < 0 || ai < 0 || ci < 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 37;
        }
        if (!(bi < ai && ai < ci)) {
            pm_util_mem_arena_destroy(arena); free(backing); return 38;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t test_graph_cycle(void) {
    void *backing = malloc(1u << 16);
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t units[2];
    const pm_metal_build_unit_t **order = NULL;
    uint32_t n_order = 0;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;

    if (!backing) return 40;
    arena = pm_util_mem_arena_create(backing, 1u << 16);
    if (!arena) { free(backing); return 41; }
    if (parse_into(arena, S_MANIFEST_CYCLE_X, &units[0]) != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 42;
    }
    if (parse_into(arena, S_MANIFEST_CYCLE_Y, &units[1]) != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 43;
    }
    rc = pm_metal_build_graph_resolve(arena, units, 2, &order, &n_order, err,
        sizeof(err));
    if (rc != PM_METAL_BUILD_ERR_CYCLE) {
        pm_util_mem_arena_destroy(arena); free(backing); return 44;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t pm_metal_build_tests(void) {
    int32_t rc;
    rc = test_parse_real_tcc_manifest();
    if (rc) return rc;
    rc = test_graph_order();
    if (rc) return rc;
    rc = test_graph_cycle();
    if (rc) return rc;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.build, tests, pm_metal_build_tests);
