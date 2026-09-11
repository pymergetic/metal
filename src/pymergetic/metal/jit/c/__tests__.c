#include "pymergetic/metal/coop.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"
#include "tccsrc_embed.inc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int32_t test_compile_alloc(void) {
    void *backing = malloc(65536);
    if (!backing) return 1;
    pm_util_mem_arena_t *arena = pm_util_mem_arena_create(backing, 65536);
    if (!arena) { free(backing); return 2; }
    pm_metal_coop_coro_t *coro = pm_metal_jit_c_compile_alloc(
        arena, "int main(){return 0;}", 21, "test_c_module");
    if (!coro) { pm_util_mem_arena_destroy(arena); free(backing); return 3; }
    if (coro->step != pm_metal_jit_c_compile_step) {
        pm_util_mem_arena_destroy(arena); free(backing); return 4;
    }
    pm_util_mem_arena_destroy(arena); free(backing);
    return 0;
}

static int32_t test_compile_real_c(void) {
    void *backing = malloc(65536);
    if (!backing) return 5;
    pm_util_mem_arena_t *arena = pm_util_mem_arena_create(backing, 65536);
    if (!arena) { free(backing); return 6; }
    pm_metal_coop_coro_t *coro = pm_metal_jit_c_compile_alloc(
        arena, "int main(){return 42;}", 22, "test_c_module");
    if (!coro) { pm_util_mem_arena_destroy(arena); free(backing); return 7; }
    pm_metal_coop_status_t st = pm_metal_jit_c_compile_step(coro);
#if PM_HAS_TCC
    if (st != PM_METAL_COOP_DONE) { pm_util_mem_arena_destroy(arena); free(backing); return 8; }
#else
    if (st != PM_METAL_COOP_ERROR) { pm_util_mem_arena_destroy(arena); free(backing); return 8; }
#endif
    pm_util_mem_arena_destroy(arena); free(backing);
    return 0;
}

static int32_t test_null_guard(void) {
    if (pm_metal_jit_c_compile_alloc(NULL, "x", 1, "m") != NULL) return 9;
    if (pm_metal_jit_c_compile_alloc(NULL, NULL, 1, "m") != NULL) return 9;
    if (pm_metal_jit_c_compile_step(NULL) != PM_METAL_COOP_ERROR) return 10;
    return 0;
}

/* object_compile_opts: include dirs + defines reach TCC. The source includes
 * a real fixture header via the include dir and reads a define both directly
 * ("NAME" -> 1) and with a value ("NAME=VALUE"); the object must compile
 * cleanly and contain the expected symbol. */
static int32_t test_object_compile_opts(void) {
#if PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    /* 32MB: the compile rides the arena now (jit.c's arena reallocator);
     * 1MB starves the tccpp pools and TCC has no NULL checks. */
    void *backing = malloc(1u << 25);
    pm_util_mem_arena_t *arena;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    char err[256];
    char incdir[512];
    const char *includes[1];
    const char *defines[3];
    static const char *src =
        "#include \"opts_fixture.h\"\n"
        "#ifndef PM_JIT_C_TEST_VAL\n"
        "#error include dir not honored\n"
        "#endif\n"
        "#if PM_JIT_C_DEFINE_BARE != 1\n"
        "#error bare define not 1\n"
        "#endif\n"
        "int pm_jit_c_opts_probe(int v) {\n"
        "    return pm_jit_c_fixture_scale(v) + PM_JIT_C_DEFINE_VAL;\n"
        "}\n";
    int32_t rc;

    if (!backing) return 20;
    arena = pm_util_mem_arena_create(backing, 1u << 25);
    if (!arena) { free(backing); return 21; }

    snprintf(incdir, sizeof(incdir), "%s", __FILE__);
    char *slash = strrchr(incdir, '/');
    if (!slash) { pm_util_mem_arena_destroy(arena); free(backing); return 22; }
    *slash = '\0';

    includes[0] = incdir;
    defines[0] = "PM_JIT_C_DEFINE_BARE";
    defines[1] = "PM_JIT_C_DEFINE_VAL=5";
    defines[2] = "PM_JIT_C_TEST_VAL=7";

    rc = pm_metal_jit_c_object_compile_opts(arena, src, strlen(src),
        includes, 1, defines, 3, &obj, &obj_len, err, sizeof(err));
    if (rc != 0) { pm_util_mem_arena_destroy(arena); free(backing); return 23; }
    if (obj == NULL || obj_len < sizeof(uint32_t)) {
        pm_util_mem_arena_destroy(arena); free(backing); return 24;
    }
    /* minimum ELF sanity: magic */
    if (obj[0] != 0x7f || obj[1] != 'E' || obj[2] != 'L' || obj[3] != 'F') {
        pm_util_mem_arena_destroy(arena); free(backing); return 25;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/* C self-host: TCC compiles TCC. libtcc.c (ONE_SOURCE — the whole
 * translation set via #include) goes through object_compile with the
 * vendored tree as the include dir and the same defines every seat's
 * build passes. The object that comes back is TCC itself, built by
 * itself, in-process — no host cc in the chain. Browser (wasm32) and
 * no-TCC seats compile this away; firmware has no PM_METAL_TCC_LIB_DIR
 * and its object path refuses politely (no temp files there), so it
 * skips rather than fails. */
static int32_t test_object_self_host_tcc(void) {
#if PM_HAS_TCC && !defined(TCC_TARGET_WASM32) && defined(PM_METAL_TCC_LIB_DIR)
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    char err[256];
    const char *includes[1];
    const char *defines[1];
    const char *src;
    unsigned src_len;
    int32_t rc;

    if (!backing) return 30;
    src = pm_metal_jit_c_tcc_source();
    src_len = pm_metal_jit_c_tcc_source_len();
    if (src == NULL || src_len < 1000) { free(backing); return 31; }

    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (!arena) { free(backing); return 32; }

    includes[0] = PM_METAL_TCC_LIB_DIR;
    defines[0] = "ONE_SOURCE";

    rc = pm_metal_jit_c_object_compile_opts(arena, src, src_len,
        includes, 1, defines, 1, &obj, &obj_len, err, sizeof(err));
    if (rc != 0) {
        pm_util_mem_arena_destroy(arena); free(backing);
        return 33;
    }
    if (obj == NULL || obj_len < 4096) {
        pm_util_mem_arena_destroy(arena); free(backing);
        return 34;
    }
    if (obj[0] != 0x7f || obj[1] != 'E' || obj[2] != 'L' || obj[3] != 'F') {
        pm_util_mem_arena_destroy(arena); free(backing);
        return 35;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/* object_compile_target: the cross knob. On an ELF seat with the second
 * (wasm32) TCC instance linked in, target=WASM32 must hand back \0asm
 * bytes; without the instance it refuses politely (rc != 0 + error text).
 * On a wasm32-native seat the knob names the seat's own backend, so the
 * object is \0asm either way. */
static int32_t test_object_compile_target(void) {
#if PM_HAS_TCC
    void *backing = malloc(1u << 25);
    pm_util_mem_arena_t *arena;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    char err[256];
    static const char *src =
        "int target_probe(int v) { return v * 3 + 1; }\n";
    int32_t rc;

    if (!backing) return 40;
    arena = pm_util_mem_arena_create(backing, 1u << 25);
    if (!arena) { free(backing); return 41; }

    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_c_object_compile_target(arena, src, strlen(src),
        NULL, 0, NULL, 0, (int32_t)PM_METAL_JIT_C_TARGET_WASM32,
        &obj, &obj_len, err, sizeof(err));
#if defined(PM_METAL_TCC_CROSS_WASM32) || defined(TCC_TARGET_WASM32)
    if (rc != 0 || obj == NULL || obj_len < 8) {
        pm_util_mem_arena_destroy(arena); free(backing); return 42;
    }
    if (obj[0] != 0x00 || obj[1] != 'a' || obj[2] != 's' || obj[3] != 'm') {
        pm_util_mem_arena_destroy(arena); free(backing); return 43;
    }
#else
    /* no wasm32 backend here: the refusal is the honest result, and the
     * errbuf must say so (a silent failure hides a dark port) */
    if (rc == 0) { pm_util_mem_arena_destroy(arena); free(backing); return 44; }
    if (err[0] == '\0') { pm_util_mem_arena_destroy(arena); free(backing); return 45; }
#endif

    /* arm-eabi cross knob: same probe, ELF magic + EM_ARM e_machine (40).
     * The armv7 ELF is a distribution artifact — linking it is the target
     * seat's business (load.c EM_ARM support), the emit is this seat's. */
    obj = NULL;
    obj_len = 0;
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_c_object_compile_target(arena, src, strlen(src),
        NULL, 0, NULL, 0, (int32_t)PM_METAL_JIT_C_TARGET_ARM_EABI,
        &obj, &obj_len, err, sizeof(err));
#if defined(PM_METAL_TCC_CROSS_ARM_EABI)
    if (rc != 0 || obj == NULL || obj_len < 52) {
        pm_util_mem_arena_destroy(arena); free(backing); return 46;
    }
    /* ELF magic + 32-bit little-endian (EI_CLASS=1, EI_DATA=1) + e_machine */
    if (obj[0] != 0x7f || obj[1] != 'E' || obj[2] != 'L' || obj[3] != 'F') {
        pm_util_mem_arena_destroy(arena); free(backing); return 47;
    }
    if (obj[4] != 1 || obj[5] != 1) {
        pm_util_mem_arena_destroy(arena); free(backing); return 48;
    }
    if (obj[18] != 40 || obj[19] != 0) {  /* EM_ARM = 40 */
        pm_util_mem_arena_destroy(arena); free(backing); return 49;
    }
#else
    /* No arm cross instance in this binary. Which outcome is correct depends
     * on the arch, not on the instance: a lane naming the seat's own arch is
     * routed to the native backend and must emit (every target producible
     * from every platform), while a genuinely foreign arch with no backend
     * must refuse and say so. */
    if (strcmp(pm_metal_jit_c_target_arch(PM_METAL_JIT_C_TARGET_ARM_EABI),
            pm_metal_jit_c_target_arch(PM_METAL_JIT_C_TARGET_SEAT)) == 0) {
        if (rc != 0 || obj == NULL || obj_len < 52) {
            pm_util_mem_arena_destroy(arena); free(backing); return 50;
        }
    } else {
        if (rc == 0) { pm_util_mem_arena_destroy(arena); free(backing); return 50; }
        if (err[0] == '\0') { pm_util_mem_arena_destroy(arena); free(backing); return 51; }
    }
#endif

    /* x86_64 cross knob (wasm32-native seat — the browser): same probe,
     * ELF64 magic + EM_X86_64 e_machine (62). Distribution artifact like
     * the arm object above; emit is this seat's, linking the target seat's. */
    obj = NULL;
    obj_len = 0;
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_c_object_compile_target(arena, src, strlen(src),
        NULL, 0, NULL, 0, (int32_t)PM_METAL_JIT_C_TARGET_X86_64,
        &obj, &obj_len, err, sizeof(err));
#if defined(PM_METAL_TCC_CROSS_X86_64)
    if (rc != 0 || obj == NULL || obj_len < 52) {
        pm_util_mem_arena_destroy(arena); free(backing); return 52;
    }
    if (obj[0] != 0x7f || obj[1] != 'E' || obj[2] != 'L' || obj[3] != 'F') {
        pm_util_mem_arena_destroy(arena); free(backing); return 53;
    }
    if (obj[4] != 2 || obj[5] != 1) {  /* ELF64, little-endian */
        pm_util_mem_arena_destroy(arena); free(backing); return 54;
    }
    if (obj[16] != 1 || obj[17] != 0) {  /* ET_REL */
        pm_util_mem_arena_destroy(arena); free(backing); return 55;
    }
    if (obj[18] != 62 || obj[19] != 0) {  /* EM_X86_64 = 62 */
        pm_util_mem_arena_destroy(arena); free(backing); return 56;
    }
#else
    /* Same rule as the arm lane above: on an x86-64 seat this lane names the
     * seat's own arch, so it must produce an ELF64 EM_X86_64 object through
     * the native backend rather than refuse for want of a cross instance. */
    if (strcmp(pm_metal_jit_c_target_arch(PM_METAL_JIT_C_TARGET_X86_64),
            pm_metal_jit_c_target_arch(PM_METAL_JIT_C_TARGET_SEAT)) == 0) {
        if (rc != 0 || obj == NULL || obj_len < 52) {
            pm_util_mem_arena_destroy(arena); free(backing); return 57;
        }
        if (obj[0] != 0x7f || obj[1] != 'E' || obj[2] != 'L' || obj[3] != 'F') {
            pm_util_mem_arena_destroy(arena); free(backing); return 58;
        }
        if (obj[18] != 62 || obj[19] != 0) {  /* EM_X86_64 = 62 */
            pm_util_mem_arena_destroy(arena); free(backing); return 59;
        }
    } else {
        if (rc == 0) { pm_util_mem_arena_destroy(arena); free(backing); return 57; }
        if (err[0] == '\0') { pm_util_mem_arena_destroy(arena); free(backing); return 58; }
    }
#endif
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/* --- diagnostic capture: invalid source names the cause --------------------
 * A refused compile must fold TCC's own diagnostic (file:line + message)
 * into errbuf, so the refusal names the real cause — never a bare
 * "compile failed" with the reason lost. */

static int32_t test_diag_invalid_source(void) {
#if PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    void *backing = malloc(1u << 24);
    pm_util_mem_arena_t *arena;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    char err[256];
    static const char *bad = "int diag_probe(v) { return undeclared_name; }\n";
    int32_t rc;

    if (!backing) return 50;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (!arena) { free(backing); return 51; }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_c_object_compile_opts(arena, bad, strlen(bad),
        NULL, 0, NULL, 0, &obj, &obj_len, err, sizeof(err));
    if (rc == 0) {
        /* compiled: TCC accepted an undeclared identifier — wrong (but a
         * compiler change, not a diag change; still a refusal expected) */
        pm_util_mem_arena_destroy(arena); free(backing); return 52;
    }
    /* the refusal must name the cause, not just the stage */
    if (strstr(err, "undeclared") == NULL) {
        fprintf(stderr, "diag invalid: err='%s'\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 53;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/* --- diagnostic isolation: a refused compile leaves no residue --------------
 * The capture is per-call (opaque pointer): a refused compile must not
 * poison the next one. Refuse, then compile GOOD source in the same
 * arena — the good compile must succeed and its errbuf must stay clean.
 * The old globals failed this shape under reentrancy: the second call's
 * capture inherited the first's tail (or wrote through a dangling
 * pointer into the first call's exited frame). */

static int32_t test_diag_isolation(void) {
#if PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    void *backing = malloc(1u << 24);
    pm_util_mem_arena_t *arena;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    char err[256];
    static const char *bad = "int iso_bad( { return 0; }\n";
    static const char *good = "int iso_good(int v) { return v + 1; }\n";
    int32_t rc;

    if (!backing) return 60;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (!arena) { free(backing); return 61; }

    /* 1. refused compile: malformed source */
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_c_object_compile_opts(arena, bad, strlen(bad),
        NULL, 0, NULL, 0, &obj, &obj_len, err, sizeof(err));
    if (rc == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 62;
    }
    if (err[0] == '\0') {
        pm_util_mem_arena_destroy(arena); free(backing); return 63;
    }

    /* 2. good compile immediately after: must succeed with a clean errbuf */
    memset(err, 0, sizeof(err));
    obj = NULL;
    obj_len = 0;
    rc = pm_metal_jit_c_object_compile_opts(arena, good, strlen(good),
        NULL, 0, NULL, 0, &obj, &obj_len, err, sizeof(err));
    if (rc != 0) {
        fprintf(stderr, "diag isolation: good compile refused: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 64;
    }
    if (err[0] != '\0') {
        fprintf(stderr, "diag isolation: residue in errbuf: '%s'\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 65;
    }
    if (obj == NULL || obj[0] != 0x7f) {
        pm_util_mem_arena_destroy(arena); free(backing); return 66;
    }

    /* 3. a second refusal right after a success: still names its cause */
    memset(err, 0, sizeof(err));
    obj = NULL;
    obj_len = 0;
    rc = pm_metal_jit_c_object_compile_opts(arena, bad, strlen(bad),
        NULL, 0, NULL, 0, &obj, &obj_len, err, sizeof(err));
    if (rc == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 67;
    }
    if (err[0] == '\0') {
        pm_util_mem_arena_destroy(arena); free(backing); return 68;
    }

    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/* Every advertised lane must build a card-shaped translation unit, and
 * every lane the mask leaves out must refuse and say why.
 *
 * The probes above compile `v * 3 + 1`, which asks a backend for nothing but
 * straight-line integer arithmetic. The wasm32 backend does exactly that and
 * no more, so it passed them while refusing — or worse, silently
 * miscompiling — every real card: the lane was advertised, a whole-tree walk
 * failed 80 of 84 units on it, and the artifacts it did emit for branches
 * loaded in an engine and returned wrong values. This canary is the smallest
 * unit shaped like a card instead: a struct, initialised static data, a call
 * between two functions, a loop and a branch. A lane that cannot compile it
 * cannot build this tree, and must not be offered as a target. */
static int32_t test_lane_mask_is_honest(void) {
#if PM_HAS_TCC
    static const char *canary =
        "struct lane_probe { int a; int b; };\n"
        "static int lane_probe_tab[3] = { 1, 2, 3 };\n"
        "static int lane_probe_add(int x, int y) { return x + y; }\n"
        "int lane_probe_main(int n) {\n"
        "    struct lane_probe s;\n"
        "    int i;\n"
        "    int acc = 0;\n"
        "    s.a = 1;\n"
        "    s.b = 2;\n"
        "    for (i = 0; i < n; i++) { acc += lane_probe_tab[i % 3]; }\n"
        "    if (acc > 100) { acc = 100; }\n"
        "    return lane_probe_add(acc, s.a + s.b);\n"
        "}\n";
    uint32_t mask = pm_metal_jit_c_target_mask();
    uint32_t t;
    void *backing = malloc(1u << 25);
    pm_util_mem_arena_t *arena;

    if (!backing) return 70;
    arena = pm_util_mem_arena_create(backing, 1u << 25);
    if (!arena) { free(backing); return 71; }

    for (t = 0; t < 4u; t++) {
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        char err[256];
        int32_t rc;
        int advertised = (mask & (1u << t)) != 0u;

        memset(err, 0, sizeof(err));
        rc = pm_metal_jit_c_object_compile_target(arena, canary,
            strlen(canary), NULL, 0, NULL, 0, (int32_t)t,
            &obj, &obj_len, err, sizeof(err));
        if (advertised) {
            /* offered as a target, so it has to deliver an object */
            if (rc != 0 || obj == NULL || obj_len < 8) {
                pm_util_mem_arena_destroy(arena); free(backing); return 72;
            }
        } else {
            /* left out, so it has to refuse rather than emit something */
            if (rc == 0) {
                pm_util_mem_arena_destroy(arena); free(backing); return 73;
            }
            if (err[0] == '\0') {
                pm_util_mem_arena_destroy(arena); free(backing); return 74;
            }
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
#else
    return 0;
#endif
}

/* The wasm32 lane's answers, not just its bytes.
 *
 * wasm32_prove.c (the standalone emitter prove) checks that a module tiles
 * and indexes in range, which is all a binary without an engine can check.
 * This seat has one: the wasmmod loader and WAMR are linked right here, and
 * they are the same pair the build card links a wasm object with. So compile
 * for the lane, load, call, compare — the loop that caught every real bug in
 * this backend (a branch that took the wrong arm, a local array whose
 * address was added twice, an i64 compare that re-tested stale flags).
 *
 * Cases stay int-in/int-out on purpose: the artifact_call transport is an
 * i32 spine (see artifact_call_wasm), so a case that wants floats or long
 * long computes with them inside and hands back an int. */
#if defined(PM_METAL_TCC_CROSS_WASM32) || defined(TCC_TARGET_WASM32)
#include "pymergetic/wasmmod/loader/__exports__.h"
#include "pymergetic/wasmmod/registry.h"
#include "wasm32_cases.inc.h"

static int32_t wasm32_run(pm_util_mem_arena_t *arena, const char *name,
    const char *src, const int *args, int n_args, const int *want) {
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    char err[256];
    char modname[96];
    pm_wasmmod_registry_handle_t h;
    int32_t rc;
    int i;
    int32_t bad = 0;

    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_c_object_compile_target(arena, src, strlen(src),
        NULL, 0, NULL, 0, (int32_t)PM_METAL_JIT_C_TARGET_WASM32,
        &obj, &obj_len, err, sizeof(err));
    if (rc != 0 || obj == NULL || obj_len == 0) {
        fprintf(stderr, "wasm32 run: %s: compile refused: %s\n", name, err);
        return 1;
    }
    /* a name of this shape is what the build card publishes under */
    snprintf(modname, sizeof(modname), "pymergetic.metal.jit.c.wasm32.%s", name);
    h = pm_wasmmod_loader_load((const uint8_t *)modname,
        (uint32_t)strlen(modname), obj, (uint32_t)obj_len);
    if (h.index == UINT32_MAX) {
        fprintf(stderr, "wasm32 run: %s: the loader refused the module"
            " (%u bytes)\n", name, (unsigned)obj_len);
        return 2;
    }
    for (i = 0; i < n_args; i++) {
        pm_wasmmod_registry_value_t a, r;
        int64_t got;
        a.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
        a.of.i32 = args[i];
        r.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
        r.of.i32 = 0;
        rc = pm_wasmmod_registry_call((const uint8_t *)modname,
            (uint32_t)strlen(modname), (const uint8_t *)"f", 1u,
            &a, 1u, &r, 1u);
        if (rc < 0) {
            fprintf(stderr, "wasm32 run: %s: f(%d) trapped (%d)\n",
                name, args[i], (int)rc);
            bad = 3;
            break;
        }
        switch (r.kind) {
        case PM_WASMMOD_REGISTRY_VALKIND_I64: got = r.of.i64; break;
        case PM_WASMMOD_REGISTRY_VALKIND_F32: got = (int64_t)r.of.f32; break;
        case PM_WASMMOD_REGISTRY_VALKIND_F64: got = (int64_t)r.of.f64; break;
        default: got = (int64_t)r.of.i32; break;
        }
        if (got != (int64_t)want[i]) {
            fprintf(stderr, "wasm32 run: %s: f(%d) = %lld, wanted %d\n",
                name, args[i], (long long)got, want[i]);
            bad = 4;
            break;
        }
    }
    (void)pm_wasmmod_loader_unload(h);
    return bad;
}

static int32_t test_wasm32_runs_real_c(void) {
    void *backing;
    pm_util_mem_arena_t *arena;
    int32_t bad = 0;
    int k;

    if ((pm_metal_jit_c_target_mask()
            & (1u << (uint32_t)PM_METAL_JIT_C_TARGET_WASM32)) == 0u) {
        return 0;               /* a seat without the lane has nothing to run */
    }
    /* the engine comes up once per process; on a seat it is boot that does
     * this, and a bare prove binary has no boot */
    if (pm_wasmmod_loader_init() != 0) {
        fprintf(stderr, "wasm32 run: the wasm runtime would not start\n");
        return 84;
    }
    backing = malloc(1u << 25);
    if (!backing) return 80;
    arena = pm_util_mem_arena_create(backing, 1u << 25);
    if (!arena) { free(backing); return 81; }

    for (k = 0; k < WASM32_CASE_COUNT; k++) {
        if (wasm32_run(arena, wasm32_cases[k].name, wasm32_cases[k].src,
                wasm32_case_args, WASM32_CASE_NARGS,
                wasm32_cases[k].want) != 0) {
            bad = 82;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return bad;
}
#else
static int32_t test_wasm32_runs_real_c(void) { return 0; }
#endif

static int32_t pm_metal_jit_c_tests(void) {
    int32_t rc;
    rc = test_compile_alloc();
    if (rc) return rc;
    rc = test_compile_real_c();
    if (rc) return rc;
    rc = test_null_guard();
    if (rc) return rc;
    rc = test_object_compile_opts();
    if (rc) return rc;
    rc = test_object_self_host_tcc();
    if (rc) return rc;
    rc = test_object_compile_target();
    if (rc) return rc;
    rc = test_lane_mask_is_honest();
    if (rc) return rc;
    rc = test_wasm32_runs_real_c();
    if (rc) return rc;
    rc = test_diag_invalid_source();
    if (rc) return rc;
    rc = test_diag_isolation();
    if (rc) return rc;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.c, tests, pm_metal_jit_c_tests);
