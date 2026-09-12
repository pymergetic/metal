/* Host test runner for metal. Tests register with PM_MOD_TEST_C/RS!; this
 * binary boots, installs the monotonic clock fill, and walks the registry —
 * the same shape as host_bench.c, but a failing test gates the build.
 *
 * Every registered module's tests run here — metal cards and the wasmmod
 * util/wasmmod C cards this binary carries (util.mem, types, wasmmod.*).
 * A test that only passes on the cargo host driver is a one-seat proof;
 * this runner is the second, independent one. */
#include "pymergetic/metal/coop/__exports__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void teardown(pm_util_mem_arena_t *arena, void *backing) {
    pm_mod_boot_unwind();
    pm_util_mem_arena_destroy(arena);
    free(backing);
}

static int32_t late_boot_init(pm_util_mem_arena_t *a) {
    (void)a;
    return 0;
}

static void late_boot_deinit(void) {}

/* host_test_cpp.cpp — the knob faces read from C++. */
int pm_metal_host_cpp_limits_prove(void);

int main(void) {
    /* Async's lock-free ready ring shrinks to fill the arena, so hand it a
     * generous span (see host_bench.c): a tight arena is what intermittently
     * made parallel tests fail with NULL (arena-exhaust). */
    enum { SPAN = 256u * 1024u * 1024u };
    void *backing = malloc(SPAN);
    pm_util_mem_arena_t *arena;
    uint32_t n, i, ran = 0, bad = 0;

    if (backing == NULL) {
        fprintf(stderr, "metal.test: malloc\n");
        return 1;
    }
    arena = pm_util_mem_arena_create(backing, SPAN);
    if (arena == NULL) {
        fprintf(stderr, "metal.test: arena_create\n");
        free(backing);
        return 1;
    }
    if (pm_mod_boot_run(arena) != 0) {
        fprintf(stderr, "metal.test: boot\n");
        teardown(arena, backing);
        return 1;
    }
    {
        static const pm_mod_boot_t late = {
            "pymergetic.test.lateboot", late_boot_init, late_boot_deinit, NULL
        };
        if (pm_mod_boot_add(&late) != 0) {
            fprintf(stderr, "metal.test: late boot add\n");
            teardown(arena, backing);
            return 1;
        }
    }

    /* Per-seat clock fill: metal's monotonic timer. */
    pm_wasmmod_registry_set_bench_clock(pm_metal_coop_mono_us);

    n = pm_wasmmod_registry_module_count();
    for (i = 0; i < n; i++) {
        uint8_t buf[256];
        uint32_t len = sizeof(buf);
        uint32_t tc;
        if (pm_wasmmod_registry_module_at(i, buf, &len) == 0 || len == 0) {
            continue;
        }
        tc = pm_wasmmod_registry_test_count(buf, len);
        if (tc == 0u) {
            continue;
        }
        ran += tc;
        if (pm_wasmmod_registry_test_run_all(buf, len) != 0) {
            bad++;
            printf("FAIL %.*s\n", (int)len, buf);
        }
    }

    /* The same knob faces from C++: a seat's transpile chain and its mrustc
     * shim are C++, so capacity has to be reachable there too. One consumer TU
     * (host_test_cpp.cpp), compiled by the C++ compiler, asks a card for its
     * knobs and moves one. */
    if (pm_metal_host_cpp_limits_prove() != 0) {
        bad++;
        printf("FAIL cpp limits prove\n");
    }

    printf("metal tests: %u run, %u not clean\n", ran, bad);
    teardown(arena, backing);
    return bad == 0 ? 0 : 1;
}
