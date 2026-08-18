/* pymergetic.metal.async — host benches (not product exports, not tests).
 *
 * A bench is `(uint64_t iters) -> i32`: do `iters` units of work inside this
 * one call, return 0 = ok. The registry (pymergetic.wasmmod.registry) owns
 * warmup + the measured lap and divides wall time by `iters` to give ns/op.
 * The host binary installs the clock (`pm_metal_async_mono_us` here), so a
 * clockless firmware seat reports "no clock" instead of a fake number.
 *
 * Two real measurements:
 *   1. ready-ring task-switch — one park->resume hop through the lock-free
 *      MPSC ready ring, per op. This is the raw cost of a coroutine scheduler.
 *   2. parallel sleeps — N tasks each sleep the same fixed dwell; wall time
 *      compresses with runner threads, so ns/op *iterations grows ~1/ncpu
 *      smaller than the serial ceiling. Shows the SMP runners overlap. */
#include "pymergetic/metal/async.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pm_metal_async_coro_t coro;
    uint64_t count;
    uint64_t target;
} bench_tick_frame_t;

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    uint64_t dwell_us;
} bench_sleep_frame_t;

static pm_metal_async_status_t step_tick(pm_metal_async_coro_t *self) {
    bench_tick_frame_t *f = (bench_tick_frame_t *)self;
    if (f->count < f->target) {
        f->count++;
        return pm_metal_async_yield_park(self);
    }
    return PM_METAL_ASYNC_DONE;
}

/* One park->resume hop (a task switch through the lock-free ready ring).
 * A single task yields `iters` times; the main thread drives it via
 * run_until, so the measured lap is the irreducible per-switch cost with no
 * cross-task contention to hide the ring CAS. (The worker threads sit idle;
 * a lone task can't livelock a 250ms stall.) */
static int32_t bench_task_switch(uint64_t iters) {
    if (iters < 1) {
        iters = 1;
    }
    bench_tick_frame_t *f = (bench_tick_frame_t *)
        pm_metal_async_coro_create(step_tick, sizeof(*f));
    if (f == NULL) {
        return -1;
    }
    f->count = 0;
    f->target = iters;
    pm_metal_async_task_t *t = pm_metal_async_create_task(&f->coro);
    if (t == NULL) {
        return -1;
    }
    if (pm_metal_async_run(t) != 0) {
        return -1;
    }
    return 0;
}

static pm_metal_async_status_t step_sleep(pm_metal_async_coro_t *self) {
    bench_sleep_frame_t *f = (bench_sleep_frame_t *)self;
    if (f->step == 0) {
        f->step = 1;
        return pm_metal_async_sleep_us(self, f->dwell_us);
    }
    return PM_METAL_ASYNC_DONE;
}

/* Parallel sleeps: one batch of up to 64 concurrent sleeps of a fixed dwell.
 * Wall time is what the registry divides by the *requested* `iters`, so the
 * ns/op reads as "wall cost per requested op when the batch overlaps at most
 * 64 ways". That is a relative SMP-overlap metric, not a per-sleep latency:
 * on a box that overlaps the batch perfectly wall stays ~dwell and the ns/op
 * stays flat as `iters` grows; a box that serializes drives wall toward
 * 64*dwell and the same ns/op balloons. Compare two seats, don't read the
 * absolute number. */
static int32_t bench_parallel_sleeps(uint64_t iters) {
    uint64_t ntasks = iters;
    if (ntasks < 1) {
        ntasks = 1;
    }
    if (ntasks > 64) {
        ntasks = 64; /* a wall-clock bench does not need a million coros */
    }
    uint64_t dwell_us = 10000ull; /* 10 ms per task */

    bench_sleep_frame_t **frames =
        (bench_sleep_frame_t **)calloc(ntasks, sizeof(*frames));
    pm_metal_async_task_t **tasks =
        (pm_metal_async_task_t **)calloc(ntasks, sizeof(*tasks));
    if (frames == NULL || tasks == NULL) {
        free(frames);
        free(tasks);
        return -1;
    }
    for (uint64_t i = 0; i < ntasks; i++) {
        frames[i] = (bench_sleep_frame_t *)
            pm_metal_async_coro_create(step_sleep, sizeof(*frames[i]));
        if (frames[i] == NULL) {
            free(frames);
            free(tasks);
            return -1;
        }
        frames[i]->dwell_us = dwell_us;
        tasks[i] = pm_metal_async_create_task(&frames[i]->coro);
        if (tasks[i] == NULL) {
            free(frames);
            free(tasks);
            return -1;
        }
    }
    for (uint64_t i = 0; i < ntasks; i++) {
        if (pm_metal_async_run(tasks[i]) != 0) {
            free(frames);
            free(tasks);
            return -1;
        }
    }
    free(frames);
    free(tasks);
    return 0;
}

PM_MOD_BENCH_C(pymergetic.metal.async, ready_ring_task_switch, bench_task_switch);
PM_MOD_BENCH_C(pymergetic.metal.async, parallel_sleeps, bench_parallel_sleeps);
