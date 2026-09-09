#include "pymergetic/metal/async.h"
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
    pm_metal_async_coro_t *coro = pm_metal_jit_c_compile_alloc(
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
    pm_metal_async_coro_t *coro = pm_metal_jit_c_compile_alloc(
        arena, "int main(){return 42;}", 22, "test_c_module");
    if (!coro) { pm_util_mem_arena_destroy(arena); free(backing); return 7; }
    pm_metal_async_status_t st = pm_metal_jit_c_compile_step(coro);
#if PM_HAS_TCC
    if (st != PM_METAL_ASYNC_DONE) { pm_util_mem_arena_destroy(arena); free(backing); return 8; }
#else
    if (st != PM_METAL_ASYNC_ERROR) { pm_util_mem_arena_destroy(arena); free(backing); return 8; }
#endif
    pm_util_mem_arena_destroy(arena); free(backing);
    return 0;
}

static int32_t test_null_guard(void) {
    if (pm_metal_jit_c_compile_alloc(NULL, "x", 1, "m") != NULL) return 9;
    if (pm_metal_jit_c_compile_alloc(NULL, NULL, 1, "m") != NULL) return 9;
    if (pm_metal_jit_c_compile_step(NULL) != PM_METAL_ASYNC_ERROR) return 10;
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
    if (rc == 0) { pm_util_mem_arena_destroy(arena); free(backing); return 50; }
    if (err[0] == '\0') { pm_util_mem_arena_destroy(arena); free(backing); return 51; }
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
    if (rc == 0) { pm_util_mem_arena_destroy(arena); free(backing); return 57; }
    if (err[0] == '\0') { pm_util_mem_arena_destroy(arena); free(backing); return 58; }
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
    rc = test_diag_invalid_source();
    if (rc) return rc;
    rc = test_diag_isolation();
    if (rc) return rc;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.c, tests, pm_metal_jit_c_tests);
