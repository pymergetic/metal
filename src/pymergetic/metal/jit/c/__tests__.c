#include "pymergetic/metal/async.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"
#include <stdlib.h>
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

static int32_t pm_metal_jit_c_tests(void) {
    int32_t rc;
    rc = test_compile_alloc();
    if (rc) return rc;
    rc = test_compile_real_c();
    if (rc) return rc;
    rc = test_null_guard();
    if (rc) return rc;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.c, tests, pm_metal_jit_c_tests);
