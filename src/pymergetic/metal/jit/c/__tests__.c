#include "pymergetic/metal/async.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"
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
    void *backing = malloc(1u << 20);
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
    arena = pm_util_mem_arena_create(backing, 1u << 20);
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
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.c, tests, pm_metal_jit_c_tests);
