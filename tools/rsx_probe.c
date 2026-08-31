/* rsx_probe.c — one-card test diagnostic: boots the registry, runs ONLY the
 * jit.rs.compiler tests entry, prints the raw return code (host_test's
 * run_all prints just FAIL with no rc). tools/ posture: not a prove gate. */
#include "pymergetic/metal/async/__exports__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    enum { SPAN = 256u * 1024u * 1024u };
    static const char fqn[] = "pymergetic.metal.jit.rs.compiler";
    void *backing = malloc(SPAN);
    pm_util_mem_arena_t *arena;
    uint32_t tc;
    int32_t rc;

    if (backing == NULL) {
        return 2;
    }
    arena = pm_util_mem_arena_create(backing, SPAN);
    if (arena == NULL) {
        free(backing);
        return 2;
    }
    if (pm_mod_boot_run(arena) != 0) {
        pm_util_mem_arena_destroy(arena);
        free(backing);
        return 3;
    }
    pm_wasmmod_registry_set_bench_clock(pm_metal_async_mono_us);
    tc = pm_wasmmod_registry_test_count((const uint8_t *)fqn, sizeof(fqn) - 1);
    printf("tests registered: %u\n", tc);
    rc = pm_wasmmod_registry_test_run_all((const uint8_t *)fqn, sizeof(fqn) - 1);
    printf("run_all rc=%d\n", rc);
    pm_mod_boot_unwind();
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}
