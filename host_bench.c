/* Host bench runner for metal. Benches register with PM_MOD_BENCH_C/RS!;
 * this binary boots, installs the monotonic clock fill, and walks the
 * registry — the same shape as host_test.c, but benches never gate. */
#include "pymergetic/metal/async/__exports__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default ops per bench. Tune so the measured lap outlives the clock tick;
 * a host clock is µs-granular and a 1M-op lap gives sub-ns/op resolution. The
 * metal clock is `pm_metal_async_mono_us`, so anything >= ~10k ops keeps the
 * 1000x multiply from truncation while still finishing fast. */
static uint64_t bench_iters(void) {
    const char *s = getenv("WASMMOD_BENCH_ITERS");
    if (s != NULL && s[0] != '\0') {
        unsigned long v = strtoul(s, NULL, 10);
        if (v > 0) {
            return (uint64_t)v;
        }
    }
    /* mod-erate default: enough ops that micro-benchmarks (task-switch) have a
     * ~10ms lap, yet few enough that wall-clock benches stay sub-second. */
    return 50000ull;
}

static int fqn_is_metal(const uint8_t *buf, uint32_t len) {
    static const char pfx[] = "pymergetic.metal";
    uint32_t n = (uint32_t)(sizeof(pfx) - 1u);
    if (len < n) {
        return 0;
    }
    if (memcmp(buf, pfx, n) != 0) {
        return 0;
    }
    return len == n || buf[n] == '.';
}

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

int main(void) {
    /* Async's lock-free ready ring shrinks to fill the arena, so hand it a
     * generous span: the ring takes ~8 MiB (1M slots > 8-byte) and the benches
     * carve N frames each lap out of the leftover. A tight arena is what made
     * parallel_sleeps intermittently return NULL (arena-exhaust) and fail. */
    enum { SPAN = 256u * 1024u * 1024u };
    void *backing = malloc(SPAN);
    pm_util_mem_arena_t *arena;
    uint64_t iters = bench_iters();
    uint32_t n, i, ran = 0, bad = 0;
    char report[1024];

    if (backing == NULL) {
        fprintf(stderr, "metal.bench: malloc\n");
        return 1;
    }
    arena = pm_util_mem_arena_create(backing, SPAN);
    if (arena == NULL) {
        fprintf(stderr, "metal.bench: arena_create\n");
        free(backing);
        return 1;
    }
    if (pm_mod_boot_run(arena) != 0) {
        fprintf(stderr, "metal.bench: boot\n");
        teardown(arena, backing);
        return 1;
    }
    {
        static const pm_mod_boot_t late = {
            "pymergetic.test.lateboot", late_boot_init, late_boot_deinit, NULL
        };
        if (pm_mod_boot_add(&late) != 0) {
            fprintf(stderr, "metal.bench: late boot add\n");
            teardown(arena, backing);
            return 1;
        }
    }

    /* Per-seat clock fill: metal's monotonic timer. Without it every bench
     * reports "no clock"; with it the registry owns warmup + measured lap. */
    pm_wasmmod_registry_set_bench_clock(pm_metal_async_mono_us);

    n = pm_wasmmod_registry_module_count();
    for (i = 0; i < n; i++) {
        uint8_t buf[256];
        uint32_t len = sizeof(buf);
        uint32_t bc;
        if (pm_wasmmod_registry_module_at(i, buf, &len) == 0 || len == 0) {
            continue;
        }
        if (!fqn_is_metal(buf, len)) {
            continue;
        }
        bc = pm_wasmmod_registry_bench_count(buf, len);
        if (bc == 0u) {
            continue;
        }
        ran += bc;
        uint32_t rlen = sizeof(report);
        bad += (uint32_t)pm_wasmmod_registry_bench_run_all(buf, len, iters,
            (uint8_t *)report, &rlen);
        /* The registry hands back raw bytes (no NUL); terminate for printf. */
        if (rlen >= sizeof(report)) {
            rlen = sizeof(report) - 1u;
        }
        report[rlen] = '\0';
        printf("%s", report);
    }

    /* Benches are informational: even a "no clock" or a failed bench does not
     * fail the run — the numbers, not a red build, are the deliverable. */
    printf("metal benches: %u in-tree, %u cleanly run (%u not clean), "
           "%llu iters each\n",
        ran, ran - bad, bad, (unsigned long long)iters);
    teardown(arena, backing);
    return 0;
}
