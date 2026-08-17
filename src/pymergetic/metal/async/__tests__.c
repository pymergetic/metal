/* pymergetic.metal.async — host prove (not product exports). */
#include "pymergetic/metal/async.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    uint32_t *counter;
    uint32_t target;
} count_frame_t;

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    pm_metal_async_coro_t *child;
} nest_frame_t;

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
} sleep_frame_t;

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.async test: %s\n", why);
    return 1;
}

static pm_metal_async_status_t step_count_yield(pm_metal_async_coro_t *self) {
    count_frame_t *f = (count_frame_t *)self;
    if (*f->counter < f->target) {
        (*f->counter)++;
        return pm_metal_async_yield_park(self);
    }
    return PM_METAL_ASYNC_DONE;
}

static pm_metal_async_status_t step_sleep(pm_metal_async_coro_t *self) {
    sleep_frame_t *f = (sleep_frame_t *)self;
    if (f->step == 0) {
        f->step = 1;
        return pm_metal_async_sleep_us(self, 2000ull);
    }
    return PM_METAL_ASYNC_DONE;
}

static pm_metal_async_status_t step_nest(pm_metal_async_coro_t *self) {
    nest_frame_t *f = (nest_frame_t *)self;
    if (f->step == 0) {
        f->step = 1;
        f->child = pm_metal_async_coro_create(step_sleep, sizeof(sleep_frame_t));
        if (f->child == NULL) {
            return PM_METAL_ASYNC_ERROR;
        }
        return pm_metal_async_await(self, f->child);
    }
    return PM_METAL_ASYNC_DONE;
}

static int32_t case_yield_two_tasks(void) {
    uint32_t a = 0;
    uint32_t b = 0;
    count_frame_t *fa = (count_frame_t *)pm_metal_async_coro_create(step_count_yield, sizeof(*fa));
    count_frame_t *fb = (count_frame_t *)pm_metal_async_coro_create(step_count_yield, sizeof(*fb));
    if (fa == NULL || fb == NULL) {
        return fail("coro");
    }
    fa->counter = &a;
    fa->target = 4;
    fb->counter = &b;
    fb->target = 4;
    pm_metal_async_task_t *ta = pm_metal_async_create_task(&fa->coro);
    pm_metal_async_task_t *tb = pm_metal_async_create_task(&fb->coro);
    if (ta == NULL || tb == NULL) {
        return fail("task");
    }
    if (pm_metal_async_run(ta) != 0) {
        return fail("run a");
    }
    if (fa->coro.status != PM_METAL_ASYNC_DONE || a != 4) {
        return fail("a done");
    }
    if (fb->coro.status != PM_METAL_ASYNC_DONE) {
        if (pm_metal_async_run(tb) != 0) {
            return fail("run b");
        }
    }
    if (fb->coro.status != PM_METAL_ASYNC_DONE || b != 4) {
        return fail("b done");
    }
    return 0;
}

static int32_t case_sleep_idle(void) {
    sleep_frame_t *f = (sleep_frame_t *)pm_metal_async_coro_create(step_sleep, sizeof(*f));
    if (f == NULL) {
        return fail("sleep coro");
    }
    pm_metal_async_task_t *t = pm_metal_async_create_task(&f->coro);
    if (t == NULL) {
        return fail("sleep task");
    }
    uint64_t t0 = pm_metal_async_mono_us();
    if (pm_metal_async_run(t) != 0) {
        return fail("sleep run");
    }
    uint64_t dt = pm_metal_async_mono_us() - t0;
    if (f->coro.status != PM_METAL_ASYNC_DONE) {
        return fail("sleep done");
    }
    if (dt < 1000ull) {
        return fail("slept too little (busy?)");
    }
    if (dt > 500000ull) {
        return fail("slept too long");
    }
    return 0;
}

static int32_t case_nested_await(void) {
    nest_frame_t *f = (nest_frame_t *)pm_metal_async_coro_create(step_nest, sizeof(*f));
    if (f == NULL) {
        return fail("nest coro");
    }
    pm_metal_async_task_t *t = pm_metal_async_create_task(&f->coro);
    if (t == NULL) {
        return fail("nest task");
    }
    if (pm_metal_async_run(t) != 0) {
        return fail("nest run");
    }
    if (f->coro.status != PM_METAL_ASYNC_DONE || f->child == NULL
        || f->child->status != PM_METAL_ASYNC_DONE) {
        return fail("nest child");
    }
    return 0;
}

static int32_t case_deadlock_empty(void) {
    sleep_frame_t *f = (sleep_frame_t *)pm_metal_async_coro_create(step_sleep, sizeof(*f));
    if (f == NULL) {
        return fail("dead coro");
    }
    /* Never create_task / never arm timer: run_until must fail, not spin. */
    if (pm_metal_async_run_until(&f->coro) == 0) {
        return fail("should deadlock-fail");
    }
    return 0;
}

static int32_t case_facade_yield(void) {
    (void)pm_metal_async_yield();
    return 0;
}

int32_t pm_metal_async_tests(void) {
    if (pm_metal_async_n_runners() < 2u) {
        return fail("ncpu");
    }
    if (case_yield_two_tasks() != 0) {
        return 1;
    }
    if (case_sleep_idle() != 0) {
        return 1;
    }
    if (case_nested_await() != 0) {
        return 1;
    }
    if (case_deadlock_empty() != 0) {
        return 1;
    }
    if (case_facade_yield() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.async, tests, pm_metal_async_tests);
