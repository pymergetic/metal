/* pymergetic.metal.jit.c — host prove.
 *
 * Without TCC linked, the compile step returns ERROR with a diagnostic.
 * This test proves the API surface compiles, links, and the error path works. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdlib.h>
#include <string.h>

static int32_t test_compile_alloc(void) {
    void *backing = malloc(65536);
    if (backing == NULL) {
        return 1;
    }
    pm_util_mem_arena_t *arena = pm_util_mem_arena_create(backing, 65536);
    if (arena == NULL) {
        free(backing);
        return 2;
    }
    pm_metal_async_coro_t *coro = pm_metal_jit_c_compile_alloc(
        arena, "int main(){return 0;}", 21, "test_c_module");
    if (coro == NULL) {
        pm_util_mem_arena_destroy(arena);
        free(backing);
        return 3;
    }
    if (coro->step != pm_metal_jit_c_compile_step) {
        pm_util_mem_arena_destroy(arena);
        free(backing);
        return 4;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t test_compile_error(void) {
    /* Without TCC, compile_step returns ERROR with a diagnostic. */
    void *backing = malloc(65536);
    if (backing == NULL) {
        return 1;
    }
    pm_util_mem_arena_t *arena = pm_util_mem_arena_create(backing, 65536);
    if (arena == NULL) {
        free(backing);
        return 2;
    }
    pm_metal_async_coro_t *coro = pm_metal_jit_c_compile_alloc(
        arena, "int main(){return 0;}", 21, "test_c_module");
    if (coro == NULL) {
        pm_util_mem_arena_destroy(arena);
        free(backing);
        return 3;
    }
    pm_metal_async_status_t st = pm_metal_jit_c_compile_step(coro);
    if (st != PM_METAL_ASYNC_ERROR) {
        pm_util_mem_arena_destroy(arena);
        free(backing);
        return 4;
    }
    pm_metal_jit_c_result_free(NULL, NULL);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t test_null_guard(void) {
    /* compile_alloc with NULL args returns NULL. */
    if (pm_metal_jit_c_compile_alloc(NULL, "x", 1, "m") != NULL) {
        return 1;
    }
    if (pm_metal_jit_c_compile_alloc(NULL, NULL, 1, "m") != NULL) {
        return 1;
    }
    /* compile_step on NULL returns ERROR. */
    if (pm_metal_jit_c_compile_step(NULL) != PM_METAL_ASYNC_ERROR) {
        return 1;
    }
    return 0;
}

static int32_t pm_metal_jit_c_tests(void) {
    if (test_compile_alloc() != 0) {
        return 1;
    }
    if (test_compile_error() != 0) {
        return 1;
    }
    if (test_null_guard() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.c, tests, pm_metal_jit_c_tests);