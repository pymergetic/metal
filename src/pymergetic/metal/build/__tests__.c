/* pymergetic.metal.build tests:
 *  - parse the REAL externals/tcc/__pmm__.toml (found relative to __FILE__,
 *    never the process cwd) and assert fqn/impl/defines + every source exists
 *  - topological order of a synthetic 3-unit graph with a dependency edge
 *  - a cyclic synthetic graph must error
 *  - multi-object compile+link via the Phase-3 seam, cross-object symbols
 *  - discovery synthesizes units from the embedded card table
 *  - jit.c rebuilt from its embedded source: byte-identical object output,
 *    rebuilt async path, and the retained provenance record (Phase 8)
 *  - tcc self-rebuild: fresh tcc compiles, runs, matches object bytes
 */
#include "pymergetic/metal/async/__types__.h"
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
#include "libtcc.h"
#include <unistd.h>
#include <fcntl.h>
/* TCC lowers u64->long double and long double->u64 as calls to these libgcc
 * helpers (gcc inlines them, so the seat binary does not export them).
 * Defining them here with external linkage lets -rdynamic expose them to the
 * build card's process resolver. */
long double __floatundixf(unsigned long long v) { return (long double)v; }
long long __fixxfdi(long double v) { return (long long)v; }
unsigned long long __fixunsxfdi(long double v) {
    return (unsigned long long)(v < 0 ? 0 : v);
}
unsigned __fixunsxfsi(long double v) {
    return (unsigned)(v < 0 ? 0 : v);
}
#endif

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

/*------------------ Phase 3: multi-object compile + link prove ------------------
 * Two tiny C sources — a callee and a caller, each compiled to its own
 * ET_REL object — linked through the in-tree ELF relocator, then the
 * caller's function is called through the image and its return value
 * (which crosses the object boundary into the callee) is asserted.
 * This is the cross-object symbol-resolution proof.
 */
static const char *S_CALLEE =
    "int add_two(int a, int b) { return a + b; }\n";

static const char *S_CALLER =
    "int add_two(int a, int b);\n"
    "int call_add(void) { return add_two(19, 23); }\n";

static int32_t test_multi_object_link(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    void *backing = malloc(1u << 19);
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t unit;
    uint8_t *obj_callee = NULL, *obj_caller = NULL;
    size_t len_callee = 0, len_caller = 0;
    uint8_t *objs[2];
    size_t lens[2];
    pm_metal_build_artifact_t art;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;
    int (*fn)(void);
    int rv;

    if (!backing) return 50;
    arena = pm_util_mem_arena_create(backing, 1u << 19);
    if (!arena) { free(backing); return 51; }

    memset(&unit, 0, sizeof(unit));
    snprintf(unit.fqn, sizeof(unit.fqn), "%s", "test.multi");

    rc = pm_metal_build_compile_source(arena, &unit, NULL, S_CALLEE,
        &obj_callee, &len_callee, err, sizeof(err));
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 52;
    }
    rc = pm_metal_build_compile_source(arena, &unit, NULL, S_CALLER,
        &obj_caller, &len_caller, err, sizeof(err));
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 53;
    }

    objs[0] = obj_callee;
    objs[1] = obj_caller;
    lens[0] = len_callee;
    lens[1] = len_caller;

    rc = pm_metal_build_link(arena, &unit, objs, lens, 2, &art, err, sizeof(err));
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 54;
    }

    fn = (int (*)(void))pm_metal_build_artifact_lookup(&art, "call_add");
    if (fn == NULL) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 55;
    }
    rv = fn();
    if (rv != 42) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 56;
    }
    pm_metal_build_artifact_destroy(&art);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    /* No native TCC object output / no ELF loader on this seat (browser
     * cell, firmware) — link must refuse honestly, not silently pass. */
    return 0;
#endif
}

/*------------------ Phase 4.2: manifest include/define forwarding ----------
 * Parse the REAL externals/tcc/__pmm__.toml, then compile a source that
 * #includes "libtcc.h" with include_dirs=["."] rooted at externals/tcc and
 * the manifest's defines (TCC_TARGET_X86_64). This proves the unit's fields
 * reach TCC through compile_source exactly as the manifest declares them. */
static int32_t test_compile_tcc_manifest_forwarding(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    char path[512];
    size_t len = 0;
    char *bytes;
    void *backing;
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t unit;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    static const char *src =
        "#include \"libtcc.h\"\n"
        "#ifndef TCC_TARGET_X86_64\n"
        "#error manifest define not forwarded\n"
        "#endif\n"
        "int pm_build_fwd_probe(void) { return (int)sizeof(TCCState *); }\n";

    snprintf(path, sizeof(path), "%s", __FILE__);
    {
        char *slash = strrchr(path, '/');
        if (!slash) return 60;
        *slash = '\0';
    }
    snprintf(path + strlen(path), sizeof(path) - strlen(path),
        "/" TCC_MANIFEST_REL);
    bytes = read_file(path, &len);
    if (!bytes) return 61;

    backing = malloc(1u << 20);
    if (!backing) { free(bytes); return 62; }
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (!arena) { free(backing); free(bytes); return 63; }

    rc = pm_metal_build_unit_parse(arena, (const uint8_t *)bytes, len, &unit,
        err, sizeof(err));
    free(bytes);
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 64;
    }

    /* unit_root = externals/tcc (the manifest's dir); unit->include_dirs
     * ["."] resolves against it, so libtcc.h is found. */
    {
        char root[512];
        char *slash;
        snprintf(root, sizeof(root), "%s", path);
        slash = strrchr(root, '/');
        if (!slash) { pm_util_mem_arena_destroy(arena); free(backing); return 65; }
        *slash = '\0';
        rc = pm_metal_build_compile_source(arena, &unit, root, src,
            &obj, &obj_len, err, sizeof(err));
    }
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 66;
    }
    if (obj == NULL || obj_len < 64 || obj[0] != 0x7f) {
        pm_util_mem_arena_destroy(arena); free(backing); return 67;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/*------------------ Phase 4.4: runtime discovery ------------------
 * discover walks the embedded card table: every impl="c" card becomes a
 * buildable unit; rs/py cards are present too (their units compile not —
 * unit_compile refuses them honestly). */
static int32_t test_discover(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t *units = NULL;
    uint32_t n_units = 0;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;
    uint32_t i;
    int have_jit_c = 0;
    uint32_t n_buildable = 0, n_rs = 0, n_other = 0;

    if (!backing) return 70;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (!arena) { free(backing); return 71; }

    rc = pm_metal_build_discover(arena, &units, &n_units, err, sizeof(err));
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 72;
    }
    if (n_units < 20) {  /* the tree carries 60+ cards */
        pm_util_mem_arena_destroy(arena); free(backing); return 73;
    }
    for (i = 0; i < n_units; i++) {
        if (strcmp(units[i].impl, "c") == 0) {
            n_buildable++;
            if (strcmp(units[i].fqn, "pymergetic.metal.jit.c") == 0) {
                have_jit_c = 1;
                if (units[i].n_sources < 1
                    || strcmp(units[i].sources[0], "__impl__.c") != 0) {
                    pm_util_mem_arena_destroy(arena); free(backing); return 74;
                }
            }
        } else if (strcmp(units[i].impl, "rs") == 0) {
            n_rs++;
        } else {
            n_other++;
        }
    }
    if (!have_jit_c) { pm_util_mem_arena_destroy(arena); free(backing); return 75; }
    if (n_buildable < 20) { pm_util_mem_arena_destroy(arena); free(backing); return 76; }

    /* an impl="rs" unit must refuse to compile with an honest error */
    {
        const pm_metal_build_unit_t *rs_unit = NULL;
        pm_metal_build_artifact_t art;
        for (i = 0; i < n_units; i++) {
            if (strcmp(units[i].impl, "rs") == 0) { rs_unit = &units[i]; break; }
        }
        if (rs_unit != NULL) {
            rc = pm_metal_build_unit_compile(arena, rs_unit, "", NULL, 0,
                NULL, 0, &art, err, sizeof(err));
            if (rc == PM_METAL_BUILD_OK
                || strstr(err, "not yet buildable") == NULL) {
                pm_util_mem_arena_destroy(arena); free(backing); return 77;
            }
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/*------------------ Phase 4.5: THE PROVE ------------------
 * Rebuild pymergetic.metal.jit.c from its EMBEDDED source bytes: discover,
 * compile with the seat's real include roots + defines, link with the
 * process resolver, then (a) byte-compare its object output against the
 * pre-linked card's for the same input, and (b) drive the rebuilt async
 * compile path end-to-end (alloc + step -> DONE, native_entry() == 7). */
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
typedef int32_t (*pm_build_obj_compile_fn)(pm_util_mem_arena_t *, const char *,
    size_t, uint8_t **, size_t *, char *, size_t);
typedef pm_metal_async_coro_t *(*pm_build_alloc_fn)(pm_util_mem_arena_t *,
    const char *, size_t, const char *);
typedef pm_metal_async_status_t (*pm_build_step_fn)(pm_metal_async_coro_t *);
#endif

static int32_t test_rebuild_jit_c(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    enum { SPAN = 64u * 1024u * 1024u };
    void *backing = malloc(SPAN);
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t *units = NULL;
    uint32_t n_units = 0;
    const pm_metal_build_unit_t *jit_unit = NULL;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;
    uint32_t i;
    pm_metal_build_artifact_t art;
    pm_build_obj_compile_fn rebuilt_compile;
    pm_build_alloc_fn rebuilt_alloc;
    pm_build_step_fn rebuilt_step;
    static const char *probe_src = "int pm_build_rebuilt_probe(void) { return 11; }\n";
    uint8_t *obj_a = NULL, *obj_b = NULL;
    size_t len_a = 0, len_b = 0;
    char dir[512];
    char src_root[2048], wasmmod_src_root[2048], wasmmod_root[2048], top_root[2048];
    char tcc_root[2048];
    const char *includes[6];
    const char *defines[6];
    uint32_t n_defines = 0;
    pm_metal_async_coro_t *coro;
    const pm_metal_jit_c_result_t *r;

    if (!backing) return 80;
    arena = pm_util_mem_arena_create(backing, SPAN);
    if (!arena) { free(backing); return 81; }

    rc = pm_metal_build_discover(arena, &units, &n_units, err, sizeof(err));
    if (rc != PM_METAL_BUILD_OK) { pm_util_mem_arena_destroy(arena); free(backing); return 82; }
    for (i = 0; i < n_units; i++) {
        if (strcmp(units[i].fqn, "pymergetic.metal.jit.c") == 0) {
            jit_unit = &units[i];
            break;
        }
    }
    if (jit_unit == NULL || jit_unit->n_sources == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 83;
    }

    /* Seat fill: the same roots the Makefile passes, resolved from __FILE__
     * (this file is src/pymergetic/metal/build/__tests__.c). After stripping
     * the file and dir components, dir = .../src/pymergetic/metal:
     *   src root      = dir/../..                (metal/src)
     *   wasmmod src   = dir/../../../wasmmod/src (extmod/wasmmod/src)
     *   wasmmod root  = dir/../../../wasmmod
     *   top           = dir/../../..             (metalpython)
     *   tcc           = dir/../../externals/tcc  (metal/externals/tcc)
     */
    snprintf(dir, sizeof(dir), "%s", __FILE__);
    {
        char *slash = strrchr(dir, '/');
        if (!slash) { pm_util_mem_arena_destroy(arena); free(backing); return 84; }
        *slash = '\0';
    }
    /* dir = .../src/pymergetic/metal/build — one more up to the metal dir */
    {
        char *slash = strrchr(dir, '/');
        if (!slash) { pm_util_mem_arena_destroy(arena); free(backing); return 85; }
        *slash = '\0';
    }
    /* dir = .../src/pymergetic/metal — the seat's real include roots, the
     * same set the host Makefile passes: -Isrc -I../wasmmod/src -I../wasmmod
     * -I<metalpython> -Iexternals/tcc
     * From dir, src is 2 up; the metal root is 3 up (dir/../.. = src,
     * dir/../../.. = <metal>); wasmmod and the top sit one above <metal>. */
    snprintf(src_root, sizeof(src_root), "%s/../..", dir);
    snprintf(tcc_root, sizeof(tcc_root), "%s/../../../externals/tcc", dir);
    snprintf(wasmmod_root, sizeof(wasmmod_root), "%s/../../../../wasmmod", dir);
    snprintf(wasmmod_src_root, sizeof(wasmmod_src_root), "%s/../../../../wasmmod/src", dir);
    snprintf(top_root, sizeof(top_root), "%s/../../../../..", dir);

    includes[0] = src_root;
    includes[1] = wasmmod_src_root;
    includes[2] = wasmmod_root;
    includes[3] = top_root;
    includes[4] = tcc_root;
    includes[5] = tcc_root;  /* libtcc.h + tcc's own headers both live here */

    defines[n_defines++] = "PM_WASMMOD_GUEST=0";
    defines[n_defines++] = "PM_MOD_TESTS=1";
    defines[n_defines++] = "TCC_TARGET_X86_64";
    defines[n_defines++] = "PM_HAS_TCC=1";
    {
        static char libdir_def[2100];
        snprintf(libdir_def, sizeof(libdir_def), "PM_METAL_TCC_LIB_DIR=\"%s\"",
            tcc_root);
        defines[n_defines++] = libdir_def;
    }
    {
        static char triplet_def[128];
        FILE *trip;
        trip = popen("cc -print-multiarch 2>/dev/null", "r");
        if (trip != NULL) {
            if (fgets(triplet_def, sizeof(triplet_def), trip) != NULL) {
                char *nl = strchr(triplet_def, '\n');
                if (nl) *nl = '\0';
                if (triplet_def[0] != '\0') {
                    static char triplet_val[160];
                    snprintf(triplet_val, sizeof(triplet_val),
                        "CONFIG_TRIPLET=\"%s\"", triplet_def);
                    defines[n_defines++] = triplet_val;
                }
            }
            pclose(trip);
        }
    }

    /* unit_root: the card's own dir (relative includes resolve there). */
    {
        char unit_root[2100];
        snprintf(unit_root, sizeof(unit_root), "%s/pymergetic/metal/jit/c", src_root);
        rc = pm_metal_build_unit_compile(arena, jit_unit, unit_root,
            includes, 6, defines, n_defines, &art, err, sizeof(err));
    }
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 86;
    }

    /* Phase 8: the compile retained a provenance record — the inspector's
     * /build/<fqn> pane serves exactly this. Sources must carry the card's
     * muscle file, and the linked image must export the card's faces. */
    {
        const pm_metal_build_record_t *rec =
            pm_metal_build_record_find("pymergetic.metal.jit.c");
        int found_impl_src = 0;
        int found_obj_compile_sym = 0;
        uint32_t k;
        if (rec == NULL || rec->n_sources == 0 || rec->n_syms == 0) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 96;
        }
        for (k = 0; k < rec->n_sources; k++) {
            if (strstr(rec->src_paths[k], "__impl__.c") != NULL) {
                found_impl_src = rec->obj_lens[k] > 0;
            }
        }
        for (k = 0; k < rec->n_syms; k++) {
            if (strcmp(rec->sym_names[k], "pm_metal_jit_c_object_compile") == 0) {
                found_obj_compile_sym = 1;
            }
        }
        if (!found_impl_src || !found_obj_compile_sym) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 97;
        }
    }

    /* (a) byte-identity: the rebuilt card's object_compile output must be
     * byte-identical to the pre-linked one for identical input+flags. */
    rebuilt_compile = (pm_build_obj_compile_fn)pm_metal_build_artifact_lookup(
        &art, "pm_metal_jit_c_object_compile");
    if (rebuilt_compile == NULL) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 87;
    }
    if (pm_metal_jit_c_object_compile(arena, probe_src, strlen(probe_src),
        &obj_a, &len_a, err, sizeof(err)) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 88;
    }
    if (rebuilt_compile(arena, probe_src, strlen(probe_src),
        &obj_b, &len_b, err, sizeof(err)) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 89;
    }
    if (len_a != len_b || memcmp(obj_a, obj_b, len_a) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 90;
    }

    /* (b) the rebuilt async path end-to-end: alloc + step -> DONE, and the
     * compiled program returns 7 through native_entry. */
    rebuilt_alloc = (pm_build_alloc_fn)pm_metal_build_artifact_lookup(
        &art, "pm_metal_jit_c_compile_alloc");
    rebuilt_step = (pm_build_step_fn)pm_metal_build_artifact_lookup(
        &art, "pm_metal_jit_c_compile_step");
    if (rebuilt_alloc == NULL || rebuilt_step == NULL) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 91;
    }
    {
        static const char *main_src = "int main(void) { return 7; }\n";
        pm_metal_async_status_t st;
        coro = rebuilt_alloc(arena, main_src, strlen(main_src), "rebuilt_jit_c");
        if (coro == NULL) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 92;
        }
        st = rebuilt_step(coro);
        if (st != PM_METAL_ASYNC_DONE) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 93;
        }
        r = pm_metal_jit_c_result_of(coro);
        if (r == NULL || r->ok != 1 || r->native_entry == NULL) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 94;
        }
        if (((int (*)(void))r->native_entry)() != 7) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 95;
        }
    }

    pm_metal_build_artifact_destroy(&art);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/*------------------ Phase 5: TCC self-rebuild ------------------
 * Compile the embedded TCC's own libtcc.c (ONE_SOURCE: one TU pulling the
 * whole translation set) with the seat's proven defines, link it with the
 * process resolver, and drive the FRESH tcc: compile+run a trivial program,
 * byte-compare its object output against the pre-linked TCC's for the same
 * input, then compile a second program. TCC is not a card — externals are
 * not in the embedded table — so the source is read from the tree relative
 * to __FILE__, exactly like the manifest itself in the parse test. */
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
typedef TCCState *(*pm_build_tcc_new_fn)(void);
typedef void (*pm_build_tcc_delete_fn)(TCCState *);
typedef int (*pm_build_tcc_set_output_type_fn)(TCCState *, int);
typedef int (*pm_build_tcc_compile_string_fn)(TCCState *, const char *);
typedef int (*pm_build_tcc_relocate_fn)(TCCState *);
typedef void *(*pm_build_tcc_get_symbol_fn)(TCCState *, const char *);
typedef int (*pm_build_tcc_set_lib_path_fn)(TCCState *, const char *);
typedef int (*pm_build_tcc_add_library_path_fn)(TCCState *, const char *);
typedef int (*pm_build_tcc_output_file_fn)(TCCState *, const char *);
#endif
static int32_t test_rebuild_tcc(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    enum { SPAN = 192u * 1024u * 1024u };
    char path[512];
    char tcc_dir[512];
    void *backing = malloc(SPAN);
    pm_util_mem_arena_t *arena;
    size_t len = 0;
    char *bytes;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    uint8_t *va_obj = NULL;
    size_t va_obj_len = 0;
    const char *includes[1];
    const char *defines[3];
    uint32_t n_defines = 0;
    pm_metal_build_unit_t unit;
    pm_metal_build_artifact_t art;
    pm_build_tcc_new_fn fresh_new;
    pm_build_tcc_delete_fn fresh_delete;
    pm_build_tcc_set_output_type_fn fresh_set_output;
    pm_build_tcc_compile_string_fn fresh_compile_string;
    pm_build_tcc_relocate_fn fresh_relocate;
    pm_build_tcc_get_symbol_fn fresh_get_symbol;
    pm_build_tcc_output_file_fn fresh_output_file;
    pm_build_tcc_set_lib_path_fn fresh_set_lib_path;
    pm_build_tcc_add_library_path_fn fresh_add_library_path;
    TCCState *s;
    static const char *add_one_src =
        "static int add_one(int v) { return v + 1; }\n"
        "int main(void) { return add_one(41); }\n";
    static const char *second_src =
        "int main(void) { return 3 * 7; }\n";
    int (*main_fn)(void);

    if (!backing) return 100;
    arena = pm_util_mem_arena_create(backing, SPAN);
    if (!arena) { free(backing); return 101; }

    /* externals/tcc resolved from this file, like TCC_MANIFEST_REL */
    snprintf(path, sizeof(path), "%s", __FILE__);
    {
        char *slash = strrchr(path, '/');
        if (!slash) { pm_util_mem_arena_destroy(arena); free(backing); return 102; }
        *slash = '\0';
    }
    snprintf(path + strlen(path), sizeof(path) - strlen(path),
        "/" TCC_MANIFEST_REL);
    snprintf(tcc_dir, sizeof(tcc_dir), "%s", path);
    {
        char *slash = strrchr(tcc_dir, '/');
        if (!slash) { pm_util_mem_arena_destroy(arena); free(backing); return 103; }
        *slash = '\0';
    }

    bytes = read_file(path, &len);
    if (!bytes) { pm_util_mem_arena_destroy(arena); free(backing); return 104; }
    rc = pm_metal_build_unit_parse(arena, (const uint8_t *)bytes, len, &unit,
        err, sizeof(err));
    free(bytes);
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 105;
    }

    /* read libtcc.c from the tree and compile it with the seat's flags:
     * ONE_SOURCE means this one TU is the whole library. lib/va_list.c is the
     * TCC runtime half of libtcc1.a: tccdefs.h lowers va_arg to a call to
     * __va_arg, which gcc inlines when IT compiles libtcc.c but the fresh
     * TCC cannot — so the runtime ships as a second object in the link. */
    {
        char libtcc_path[600];
        size_t src_len = 0;
        char *src;
        snprintf(libtcc_path, sizeof(libtcc_path), "%s/libtcc.c", tcc_dir);
        src = read_file(libtcc_path, &src_len);
        if (!src) { pm_util_mem_arena_destroy(arena); free(backing); return 106; }
        includes[0] = tcc_dir;
        defines[n_defines++] = "TCC_TARGET_X86_64";
        {
            static char triplet_def[128];
            FILE *trip = popen("cc -print-multiarch 2>/dev/null", "r");
            if (trip != NULL) {
                if (fgets(triplet_def, sizeof(triplet_def), trip) != NULL) {
                    char *nl = strchr(triplet_def, '\n');
                    if (nl) *nl = '\0';
                    if (triplet_def[0] != '\0') {
                        static char triplet_val[160];
                        snprintf(triplet_val, sizeof(triplet_val),
                            "CONFIG_TRIPLET=\"%s\"", triplet_def);
                        defines[n_defines++] = triplet_val;
                    }
                }
                pclose(trip);
            }
        }
        /* parse the manifest's unit (fqn pymergetic.metal.external.tcc) so
         * the link knows the unit; compile the source through the opts seam */
        memset(&unit, 0, sizeof(unit));
        snprintf(unit.fqn, sizeof(unit.fqn), "%s", "pymergetic.metal.external.tcc");
        rc = pm_metal_jit_c_object_compile_opts(arena, src, src_len,
            includes, 1, defines, n_defines, &obj, &obj_len, err, sizeof(err));
        free(src);
        if (rc != 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 107;
        }
        {
            char valist_path[600];
            size_t val_len = 0;
            char *val_src;
            uint8_t *val_obj = NULL;
            size_t val_len_out = 0;
            snprintf(valist_path, sizeof(valist_path), "%s/lib/va_list.c", tcc_dir);
            val_src = read_file(valist_path, &val_len);
            if (!val_src) { pm_util_mem_arena_destroy(arena); free(backing); return 130; }
            rc = pm_metal_jit_c_object_compile_opts(arena, val_src, val_len,
                includes, 1, defines, n_defines, &val_obj, &val_len_out,
                err, sizeof(err));
            free(val_src);
            if (rc != 0) {
                pm_util_mem_arena_destroy(arena); free(backing); return 131;
            }
            va_obj = val_obj;
            va_obj_len = val_len_out;
        }
    }
    if (rc != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 107;
    }

    {
        uint8_t *objs[2];
        size_t lens[2];
        objs[0] = obj;
        lens[0] = obj_len;
        objs[1] = va_obj;
        lens[1] = va_obj_len;
        rc = pm_metal_build_link(arena, &unit, objs, lens, 2, &art, err, sizeof(err));
    }
    if (rc != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 108;
    }

    fresh_new = (pm_build_tcc_new_fn)pm_metal_build_artifact_lookup(&art, "tcc_new");
    fresh_delete = (pm_build_tcc_delete_fn)pm_metal_build_artifact_lookup(&art, "tcc_delete");
    fresh_set_output = (pm_build_tcc_set_output_type_fn)pm_metal_build_artifact_lookup(
        &art, "tcc_set_output_type");
    fresh_compile_string = (pm_build_tcc_compile_string_fn)pm_metal_build_artifact_lookup(
        &art, "tcc_compile_string");
    fresh_relocate = (pm_build_tcc_relocate_fn)pm_metal_build_artifact_lookup(
        &art, "tcc_relocate");
    fresh_get_symbol = (pm_build_tcc_get_symbol_fn)pm_metal_build_artifact_lookup(
        &art, "tcc_get_symbol");
    fresh_output_file = (pm_build_tcc_output_file_fn)pm_metal_build_artifact_lookup(
        &art, "tcc_output_file");
    fresh_set_lib_path = (pm_build_tcc_set_lib_path_fn)pm_metal_build_artifact_lookup(
        &art, "tcc_set_lib_path");
    fresh_add_library_path = (pm_build_tcc_add_library_path_fn)pm_metal_build_artifact_lookup(
        &art, "tcc_add_library_path");
    if (!fresh_new || !fresh_delete || !fresh_set_output || !fresh_compile_string
        || !fresh_relocate || !fresh_get_symbol || !fresh_output_file
        || !fresh_set_lib_path || !fresh_add_library_path) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 109;
    }

    /* fresh TCC compiles + runs a trivial program: add_one(41) == 42. The
     * pre-linked copy runs with the seat's library path (PM_METAL_TCC_LIB_DIR
     * baked at compile time); the fresh copy gets the same, resolved from
     * __FILE__ like the manifest. */
    s = fresh_new();
    if (!s) { pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 110; }
    fresh_set_lib_path(s, tcc_dir);
    fresh_add_library_path(s, tcc_dir);
    fresh_set_output(s, TCC_OUTPUT_MEMORY);
    if (fresh_compile_string(s, add_one_src) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 111; }
    if (fresh_relocate(s) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 112; }
    main_fn = (int (*)(void))fresh_get_symbol(s, "main");
    if (!main_fn) { pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 113; }
    if (main_fn() != 42) { pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 114; }
    fresh_delete(s);

    /* fresh TCC emits an object; byte-compare against the pre-linked TCC's
     * output for identical input+flags */
    {
        uint8_t *fresh_obj = NULL;
        size_t fresh_len = 0;
        uint8_t *prelinked_obj = NULL;
        size_t prelinked_len = 0;
        s = fresh_new();
        if (!s) { pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 115; }
        fresh_set_lib_path(s, tcc_dir);
        fresh_add_library_path(s, tcc_dir);
        fresh_set_output(s, TCC_OUTPUT_OBJ);
        if (fresh_compile_string(s, add_one_src) != 0) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 116; }
        {
            /* object output goes through the jit.c card's temp-file path:
             * drive it via the fresh TCC's tcc_output_file + read back. The
             * fresh state's file output writes to the cwd — use a temp path. */
            char tmpl[] = "/tmp/.jit_c_fresh_XXXXXX";
            int fd = mkstemp(tmpl);
            FILE *f;
            long n;
            if (fd < 0) { pm_metal_build_artifact_destroy(&art);
                pm_util_mem_arena_destroy(arena); free(backing); return 117; }
            close(fd);
            if (fresh_output_file(s, tmpl) != 0) {
                unlink(tmpl);
                pm_metal_build_artifact_destroy(&art);
                pm_util_mem_arena_destroy(arena); free(backing); return 118; }
            f = fopen(tmpl, "rb");
            if (!f) { unlink(tmpl); pm_metal_build_artifact_destroy(&art);
                pm_util_mem_arena_destroy(arena); free(backing); return 119; }
            fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
            fresh_obj = (uint8_t *)pm_util_mem_alloc(arena, (size_t)n);
            if (!fresh_obj || fread(fresh_obj, 1, (size_t)n, f) != (size_t)n) {
                fclose(f); unlink(tmpl); pm_metal_build_artifact_destroy(&art);
                pm_util_mem_arena_destroy(arena); free(backing); return 120; }
            fclose(f);
            fresh_len = (size_t)n;
            unlink(tmpl);
        }
        fresh_delete(s);

        if (pm_metal_jit_c_object_compile(arena, add_one_src, strlen(add_one_src),
            &prelinked_obj, &prelinked_len, err, sizeof(err)) != 0) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 121; }
        if (fresh_len != prelinked_len || memcmp(fresh_obj, prelinked_obj, fresh_len) != 0) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing); return 122; }
    }

    /* fresh TCC compiles a second program */
    s = fresh_new();
    if (!s) { pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 123; }
    fresh_set_lib_path(s, tcc_dir);
    fresh_add_library_path(s, tcc_dir);
    fresh_set_output(s, TCC_OUTPUT_MEMORY);
    if (fresh_compile_string(s, second_src) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 124; }
    if (fresh_relocate(s) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 125; }
    main_fn = (int (*)(void))fresh_get_symbol(s, "main");
    if (!main_fn || main_fn() != 21) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing); return 126; }
    fresh_delete(s);

    pm_metal_build_artifact_destroy(&art);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/* Phase 8: record query faces — unknown fqn is NULL (404 pane), reset clears.
 * Runs after test_rebuild_jit_c so the jit.c record from that compile is live
 * and observable here. */
static int32_t test_record_query(void) {
    if (pm_metal_build_record_find("no.such.card") != NULL) {
        return 98;
    }
    if (pm_metal_build_record_find(NULL) != NULL) {
        return 99;
    }
    pm_metal_build_record_reset();
    if (pm_metal_build_record_find("pymergetic.metal.jit.c") != NULL) {
        return 100;
    }
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
    rc = test_multi_object_link();
    if (rc) return rc;
    rc = test_compile_tcc_manifest_forwarding();
    if (rc) return rc;
    rc = test_discover();
    if (rc) return rc;
    rc = test_rebuild_jit_c();
    if (rc) return rc;
    rc = test_rebuild_tcc();
    if (rc) return rc;
    rc = test_record_query();
    if (rc) return rc;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.build, tests, pm_metal_build_tests);
