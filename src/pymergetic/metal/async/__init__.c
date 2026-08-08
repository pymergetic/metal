/*
 * pymergetic.metal.async — C impl (N equal runners).
 * Public face: include/pymergetic/metal/async/… (see docs/HYBRID.md).
 */
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/async/smp.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_ASYNC_MAX_HANDLES
#define PM_METAL_ASYNC_MAX_HANDLES 64
#endif
#ifndef PM_METAL_ASYNC_MAX_RUNNERS
#define PM_METAL_ASYNC_MAX_RUNNERS 8
#endif
#ifndef PM_METAL_ASYNC_READY_CAP
#define PM_METAL_ASYNC_READY_CAP 64
#endif

typedef enum {
    SLOT_FREE = 0,
    SLOT_SLEEP,
    SLOT_YIELD,
    SLOT_AWAIT
} slot_kind_t;

typedef struct {
    uint8_t used;
    uint8_t kind;
    uint8_t status;
    uint8_t runner;
    uint64_t deadline_us;
    uint32_t await_child;
    uint32_t result_u32;
} slot_t;

typedef struct {
    uint32_t q[PM_METAL_ASYNC_READY_CAP];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} runner_t;

static slot_t g_slots[PM_METAL_ASYNC_MAX_HANDLES];
static runner_t g_runners[PM_METAL_ASYNC_MAX_RUNNERS];
static volatile uint32_t g_runner_lock[PM_METAL_ASYNC_MAX_RUNNERS];
static uint32_t g_n_runners;
static uint32_t g_rr;
static int g_started;
static uint64_t g_boot_us;
static pm_metal_async_idle_pump_fn g_idle_pump;

static void runner_lock(uint32_t ri)
{
    while (__atomic_exchange_n(&g_runner_lock[ri], 1u, __ATOMIC_ACQUIRE) != 0u) {
        __asm__ volatile("pause");
    }
}

static void runner_unlock(uint32_t ri)
{
    __atomic_store_n(&g_runner_lock[ri], 0u, __ATOMIC_RELEASE);
}

void pm_metal_async_set_idle_pump(pm_metal_async_idle_pump_fn fn)
{
    g_idle_pump = fn;
}

void pm_metal_time_init(void)
{
    g_boot_us = pm_metal_board_mono_us();
}

uint64_t pm_metal_async_mono_us(void)
{
    return pm_metal_board_mono_us() - g_boot_us;
}

uint64_t pm_metal_time_mono_us(void)
{
    return pm_metal_async_mono_us();
}

static int ready_push(uint32_t ri, uint32_t h)
{
    runner_t *r;
    int rc = -1;

    if (ri >= g_n_runners || h == 0) {
        return -1;
    }
    runner_lock(ri);
    r = &g_runners[ri];
    if (r->count < PM_METAL_ASYNC_READY_CAP) {
        r->q[r->tail] = h;
        r->tail = (r->tail + 1u) % PM_METAL_ASYNC_READY_CAP;
        r->count++;
        rc = 0;
    }
    runner_unlock(ri);
    return rc;
}

static uint32_t ready_pop(uint32_t ri)
{
    runner_t *r;
    uint32_t h = 0;

    if (ri >= g_n_runners) {
        return 0;
    }
    runner_lock(ri);
    r = &g_runners[ri];
    if (r->count != 0u) {
        h = r->q[r->head];
        r->head = (r->head + 1u) % PM_METAL_ASYNC_READY_CAP;
        r->count--;
    }
    runner_unlock(ri);
    return h;
}

static uint32_t next_runner(void)
{
    uint32_t ri;

    if (g_n_runners == 0u) {
        return 0;
    }
    ri = g_rr % g_n_runners;
    g_rr++;
    return ri;
}

static uint32_t alloc_slot(slot_kind_t kind, uint64_t deadline_us, uint32_t runner)
{
    uint32_t i;

    for (i = 1; i < PM_METAL_ASYNC_MAX_HANDLES; i++) {
        if (!g_slots[i].used) {
            g_slots[i].used = 1;
            g_slots[i].kind = (uint8_t)kind;
            g_slots[i].status = PM_METAL_ASYNC_WAITING;
            g_slots[i].runner = (uint8_t)runner;
            g_slots[i].deadline_us = deadline_us;
            g_slots[i].await_child = 0;
            g_slots[i].result_u32 = 0;
            return i;
        }
    }
    return 0;
}

pm_metal_async_status_t pm_metal_async_status(uint32_t h)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return PM_METAL_ASYNC_ERROR;
    }
    return (pm_metal_async_status_t)__atomic_load_n(&g_slots[h].status, __ATOMIC_ACQUIRE);
}

void pm_metal_async_set_result_u32(uint32_t h, uint32_t v)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return;
    }
    g_slots[h].result_u32 = v;
}

uint32_t pm_metal_async_result_u32(uint32_t h)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return 0;
    }
    return g_slots[h].result_u32;
}

uint32_t pm_metal_async_sleep_us(uint64_t us)
{
    uint32_t ri = next_runner();
    return alloc_slot(SLOT_SLEEP, pm_metal_async_mono_us() + us, ri);
}

uint32_t pm_metal_async_sleep_until_us(uint64_t deadline_us)
{
    uint32_t ri = next_runner();
    return alloc_slot(SLOT_SLEEP, deadline_us, ri);
}

uint32_t pm_metal_async_sleep(uint32_t ms)
{
    return pm_metal_async_sleep_us((uint64_t)ms * 1000ull);
}

uint32_t pm_metal_async_yield(void)
{
    uint32_t ri = next_runner();
    uint32_t h = alloc_slot(SLOT_YIELD, 0, ri);

    if (h != 0u) {
        (void)ready_push(ri, h);
    }
    return h;
}

uint32_t pm_metal_async_create_task(uint32_t h)
{
    uint32_t ri;

    if (!g_started) {
        return 0;
    }
    if (h == 0u) {
        return pm_metal_async_yield();
    }
    if (h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return 0;
    }
    ri = next_runner();
    g_slots[h].runner = (uint8_t)ri;
    if (g_slots[h].kind == SLOT_YIELD
        || g_slots[h].status == PM_METAL_ASYNC_PENDING) {
        (void)ready_push(ri, h);
    }
    return h;
}

int32_t pm_metal_async_start(uint32_t n_cpus)
{
    uint32_t n;
    uint32_t i;

    memset(g_slots, 0, sizeof(g_slots));
    memset(g_runners, 0, sizeof(g_runners));
    n = n_cpus == 0u ? 1u : n_cpus;
    if (n > PM_METAL_ASYNC_MAX_RUNNERS) {
        n = PM_METAL_ASYNC_MAX_RUNNERS;
    }
    g_n_runners = n;
    g_rr = 0;
    g_started = 1;
    g_idle_pump = NULL;
    for (i = 0; i < n; i++) {
        g_runners[i].head = 0;
        g_runners[i].tail = 0;
        g_runners[i].count = 0;
    }
    pm_metal_time_init();
    return 0;
}

int32_t pm_metal_async_ready(void)
{
    return g_started ? 1 : 0;
}

uint32_t pm_metal_async_n_runners(void)
{
    return g_n_runners;
}

static void complete_slot(uint32_t i)
{
    __atomic_store_n(&g_slots[i].status, (uint8_t)PM_METAL_ASYNC_DONE, __ATOMIC_RELEASE);
}

static int32_t poll_runner(uint32_t ri, uint64_t now)
{
    uint32_t i;
    uint32_t h;
    int32_t n = 0;
    uint32_t steps;

    /* Due sleeps owned by this runner. */
    for (i = 1; i < PM_METAL_ASYNC_MAX_HANDLES; i++) {
        if (!g_slots[i].used || g_slots[i].status != PM_METAL_ASYNC_WAITING) {
            continue;
        }
        if (g_slots[i].runner != (uint8_t)ri) {
            continue;
        }
        if (g_slots[i].kind == SLOT_SLEEP && now >= g_slots[i].deadline_us) {
            complete_slot(i);
            n++;
        } else if (g_slots[i].kind == SLOT_AWAIT) {
            uint32_t c = g_slots[i].await_child;

            if (c == 0 || c >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[c].used) {
                g_slots[i].status = PM_METAL_ASYNC_ERROR;
                n++;
            } else if (g_slots[c].status == PM_METAL_ASYNC_DONE
                       || g_slots[c].status == PM_METAL_ASYNC_ERROR
                       || g_slots[c].status == PM_METAL_ASYNC_CANCELLED) {
                g_slots[i].status = g_slots[c].status;
                g_slots[i].result_u32 = g_slots[c].result_u32;
                n++;
            }
        }
    }

    /* Drain ready ring (yields / create_task). */
    steps = 0;
    while (steps < PM_METAL_ASYNC_READY_CAP) {
        h = ready_pop(ri);
        if (h == 0u) {
            break;
        }
        steps++;
        if (h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
            continue;
        }
        if (g_slots[h].status != PM_METAL_ASYNC_WAITING) {
            continue;
        }
        if (g_slots[h].kind == SLOT_YIELD) {
            complete_slot(h);
            n++;
        }
    }
    return n;
}

int32_t pm_metal_async_run_poll_cpu(uint32_t cpu)
{
    uint64_t now;

    if (!g_started || cpu >= g_n_runners) {
        return -1;
    }
    /* Net/device idle pump stays on BSP — not concurrent on APs. */
    if (cpu == 0u && g_idle_pump != NULL) {
        g_idle_pump();
    }
    now = pm_metal_async_mono_us();
    return poll_runner(cpu, now);
}

int32_t pm_metal_async_run_poll(void)
{
    uint64_t now;
    uint32_t ri;
    int32_t n = 0;

    if (!g_started) {
        return -1;
    }
    /* After real SMP, each CPU drains only its runner. */
    if (pm_metal_smp_online_count() > 1u) {
        return pm_metal_async_run_poll_cpu(pm_metal_smp_cpu_index());
    }
    if (g_idle_pump != NULL) {
        g_idle_pump();
    }
    now = pm_metal_async_mono_us();
    for (ri = 0; ri < g_n_runners; ri++) {
        n += poll_runner(ri, now);
    }
    return n;
}

uint32_t pm_metal_async_create_task_on(uint32_t h, uint32_t runner)
{
    if (!g_started || runner >= g_n_runners) {
        return 0;
    }
    if (h == 0u) {
        h = alloc_slot(SLOT_YIELD, 0, runner);
        if (h == 0u) {
            return 0;
        }
        (void)ready_push(runner, h);
        return h;
    }
    if (h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return 0;
    }
    g_slots[h].runner = (uint8_t)runner;
    if (g_slots[h].kind == SLOT_YIELD
        || g_slots[h].status == PM_METAL_ASYNC_PENDING) {
        (void)ready_push(runner, h);
    }
    return h;
}

volatile uint32_t pm_metal_smp_poll_ticks[PM_METAL_ASYNC_MAX_RUNNERS];

int32_t pm_metal_async_run_loop_cpu(uint32_t cpu)
{
    if (!g_started || cpu >= g_n_runners) {
        return -1;
    }
    for (;;) {
        (void)pm_metal_async_run_poll_cpu(cpu);
        __atomic_fetch_add(&pm_metal_smp_poll_ticks[cpu], 1u, __ATOMIC_RELAXED);
        __asm__ volatile("pause");
    }
}

int32_t pm_metal_async_run_poll_all(void)
{
    int32_t total = 0;
    int32_t n;
    int guard = 0;

    do {
        n = pm_metal_async_run_poll();
        if (n < 0) {
            return n;
        }
        total += n;
        guard++;
    } while (n > 0 && guard < 1000);
    return total;
}

static int any_waiting(void)
{
    uint32_t i;

    for (i = 1; i < PM_METAL_ASYNC_MAX_HANDLES; i++) {
        if (g_slots[i].used && g_slots[i].status == PM_METAL_ASYNC_WAITING) {
            return 1;
        }
    }
    return 0;
}

int32_t pm_metal_async_run_loop(void)
{
    int32_t total = 0;
    int guard = 0;

    if (!g_started) {
        return -1;
    }
    while (guard < 10000) {
        int32_t n = pm_metal_async_run_poll_all();

        if (n < 0) {
            return n;
        }
        total += n;
        if (!any_waiting()) {
            return total;
        }
        if (n == 0) {
            /* Idle with future sleeps still WAITING — caller drives time. */
            return total;
        }
        guard++;
    }
    return total;
}

pm_metal_async_status_t pm_metal_async_await(uint32_t self_h, uint32_t child_h)
{
    uint32_t ri;
    uint32_t ah;

    if (child_h == 0 || child_h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[child_h].used) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (self_h != 0 && self_h < PM_METAL_ASYNC_MAX_HANDLES && g_slots[self_h].used) {
        g_slots[self_h].kind = SLOT_AWAIT;
        g_slots[self_h].status = PM_METAL_ASYNC_WAITING;
        g_slots[self_h].await_child = child_h;
        (void)pm_metal_async_run_poll();
        return pm_metal_async_status(self_h);
    }
    /* No self handle: allocate await on RR runner. */
    ri = next_runner();
    ah = alloc_slot(SLOT_AWAIT, 0, ri);
    if (ah == 0u) {
        return PM_METAL_ASYNC_ERROR;
    }
    g_slots[ah].await_child = child_h;
    (void)pm_metal_async_run_poll();
    return pm_metal_async_status(ah);
}
