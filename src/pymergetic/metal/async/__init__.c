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
#include "pymergetic/metal/async/prio.h"
#include "pymergetic/metal/async/coro.h"
#include "pymergetic/metal/async/task.h"
#include "pymergetic/metal/async/quiesce.h"
#include "pymergetic/metal/async/meter.h"
#include "pymergetic/metal/mem.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_ASYNC_METER
#define PM_METAL_ASYNC_METER 1
#endif

#ifndef PM_METAL_ASYNC_MAX_HANDLES
#define PM_METAL_ASYNC_MAX_HANDLES 64
#endif
#ifndef PM_METAL_ASYNC_MAX_RUNNERS
#define PM_METAL_ASYNC_MAX_RUNNERS 8
#endif
#ifndef PM_METAL_ASYNC_READY_CAP
#define PM_METAL_ASYNC_READY_CAP 64
#endif
#define PM_METAL_ASYNC_N_PRIO 3u

#if defined(__wasm__) || defined(__EMSCRIPTEN__)
#define PM_METAL_CPU_PAUSE() ((void)0)
#else
#define PM_METAL_CPU_PAUSE() __asm__ volatile("pause")
#endif
typedef enum {
    SLOT_FREE = 0,
    SLOT_SLEEP,
    SLOT_YIELD,
    SLOT_AWAIT,
    SLOT_PARK,
    SLOT_CORO
} slot_kind_t;

typedef struct {
    uint8_t used;
    uint8_t kind;
    uint8_t status;
    uint8_t runner;
    uint8_t prio;
    uint64_t deadline_us;
    uint32_t await_child;
    uint32_t result_u32;
    pm_metal_async_step_fn_t step;
    uint8_t *frame;
    uint32_t frame_bytes;
} slot_t;

typedef struct {
    uint32_t q[PM_METAL_ASYNC_READY_CAP];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} prio_q_t;

typedef struct {
    prio_q_t ready[PM_METAL_ASYNC_N_PRIO]; /* H M L */
    uint32_t usage[PM_METAL_ASYNC_N_PRIO];
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
        PM_METAL_CPU_PAUSE();
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

uint64_t pm_metal_async_mono_ms(void)
{
    return pm_metal_async_mono_us() / 1000ull;
}

uint64_t pm_metal_time_mono_us(void)
{
    return pm_metal_async_mono_us();
}

void pm_metal_time_sleep_us(uint64_t us)
{
    /* Chunk so idle pump / async can run during long waits (e.g. unboot 2s). */
    while (us > 0ull) {
        uint64_t chunk = us > 1000ull ? 1000ull : us;

        pm_metal_board_sleep_us(chunk);
        pm_metal_board_time_advance_us(chunk);
        us -= chunk;
        (void)pm_metal_async_run_poll();
    }
}

void pm_metal_time_sleep_ms(uint32_t ms)
{
    pm_metal_time_sleep_us((uint64_t)ms * 1000ull);
}

static int ready_push_prio(uint32_t ri, uint32_t h, uint8_t prio)
{
    runner_t *r;
    prio_q_t *q;
    int rc = -1;

    if (ri >= g_n_runners || h == 0 || prio >= PM_METAL_ASYNC_N_PRIO) {
        return -1;
    }
    runner_lock(ri);
    r = &g_runners[ri];
    q = &r->ready[prio];
    if (q->count < PM_METAL_ASYNC_READY_CAP) {
        q->q[q->tail] = h;
        q->tail = (q->tail + 1u) % PM_METAL_ASYNC_READY_CAP;
        q->count++;
        g_slots[h].prio = prio;
        rc = 0;
    }
    runner_unlock(ri);
    return rc;
}

static int ready_push(uint32_t ri, uint32_t h)
{
    uint8_t p = PM_METAL_ASYNC_PRIO_MED;

    if (h != 0u && h < PM_METAL_ASYNC_MAX_HANDLES && g_slots[h].used) {
        p = g_slots[h].prio;
        if (p >= PM_METAL_ASYNC_N_PRIO) {
            p = PM_METAL_ASYNC_PRIO_MED;
        }
    }
    return ready_push_prio(ri, h, p);
}

/* Pop next ready handle: timing boost already done; then H/M/L + usage weight. */
static uint32_t ready_pop(uint32_t ri)
{
    runner_t *r;
    uint32_t h = 0;
    uint32_t order[3];
    uint32_t oi;

    if (ri >= g_n_runners) {
        return 0;
    }
    runner_lock(ri);
    r = &g_runners[ri];
    /* Default High→Med→Low; if Low starved vs High, try Low earlier. */
    order[0] = PM_METAL_ASYNC_PRIO_HIGH;
    order[1] = PM_METAL_ASYNC_PRIO_MED;
    order[2] = PM_METAL_ASYNC_PRIO_LOW;
    if (r->usage[PM_METAL_ASYNC_PRIO_HIGH] > (r->usage[PM_METAL_ASYNC_PRIO_LOW] + 8u) &&
        r->ready[PM_METAL_ASYNC_PRIO_LOW].count > 0u) {
        order[0] = PM_METAL_ASYNC_PRIO_LOW;
        order[1] = PM_METAL_ASYNC_PRIO_HIGH;
        order[2] = PM_METAL_ASYNC_PRIO_MED;
    }
    for (oi = 0; oi < 3u; oi++) {
        prio_q_t *q = &r->ready[order[oi]];
        if (q->count == 0u) {
            continue;
        }
        h = q->q[q->head];
        q->head = (q->head + 1u) % PM_METAL_ASYNC_READY_CAP;
        q->count--;
        r->usage[order[oi]]++;
        break;
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
            g_slots[i].prio = (uint8_t)PM_METAL_ASYNC_PRIO_MED;
            g_slots[i].deadline_us = deadline_us;
            g_slots[i].await_child = 0;
            g_slots[i].result_u32 = 0;
            g_slots[i].step = NULL;
            g_slots[i].frame = NULL;
            g_slots[i].frame_bytes = 0;
            return i;
        }
    }
    return 0;
}

void pm_metal_async_set_prio(uint32_t h, pm_metal_async_prio_t prio)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return;
    }
    if ((uint32_t)prio >= PM_METAL_ASYNC_N_PRIO) {
        prio = PM_METAL_ASYNC_PRIO_MED;
    }
    g_slots[h].prio = (uint8_t)prio;
}

pm_metal_async_prio_t pm_metal_async_get_prio(uint32_t h)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return PM_METAL_ASYNC_PRIO_MED;
    }
    return (pm_metal_async_prio_t)g_slots[h].prio;
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

/* Already-complete handle (DONE) carrying v — sync backends on async APIs. */
uint32_t pm_metal_async_completed_u32(uint32_t v)
{
    uint32_t h = alloc_slot(SLOT_YIELD, 0, next_runner());
    if (h == 0) {
        return 0;
    }
    g_slots[h].result_u32 = v;
    __atomic_store_n(&g_slots[h].status, (uint8_t)PM_METAL_ASYNC_DONE, __ATOMIC_RELEASE);
    return h;
}

void pm_metal_async_coro_close(uint32_t h)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return;
    }
    if (g_slots[h].frame != NULL) {
        pm_metal_mem_free(g_slots[h].frame);
    }
    memset(&g_slots[h], 0, sizeof(g_slots[h]));
}

uint32_t pm_metal_async_await_child(uint32_t h)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return 0;
    }
    return g_slots[h].await_child;
}

void pm_metal_async_cancel_tree(uint32_t h)
{
    uint32_t guard = 0;
    while (h != 0u && h < PM_METAL_ASYNC_MAX_HANDLES && g_slots[h].used && guard < PM_METAL_ASYNC_MAX_HANDLES) {
        uint32_t child = g_slots[h].await_child;
        g_slots[h].await_child = 0;
        pm_metal_async_coro_close(h);
        h = child;
        guard++;
    }
}

uint32_t pm_metal_async_coro_create(pm_metal_async_step_fn_t step, uint32_t state_bytes)
{
    uint32_t h;
    uint8_t *frame = NULL;

    if (step == NULL || !g_started) {
        return 0;
    }
    if (state_bytes > 0u) {
        frame = pm_metal_mem_alloc((size_t)state_bytes);
        if (frame == NULL) {
            return 0;
        }
        memset(frame, 0, (size_t)state_bytes);
    }
    h = alloc_slot(SLOT_CORO, 0, next_runner());
    if (h == 0u) {
        if (frame != NULL) {
            pm_metal_mem_free(frame);
        }
        return 0;
    }
    g_slots[h].step = step;
    g_slots[h].frame = frame;
    g_slots[h].frame_bytes = state_bytes;
    g_slots[h].status = (uint8_t)PM_METAL_ASYNC_PENDING;
    return h;
}

void *pm_metal_async_coro_state(uint32_t h)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return NULL;
    }
    return g_slots[h].frame;
}

void *pm_metal_async_coro_alloc(uint32_t h, uint32_t n)
{
    uint8_t *frame;

    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used || n == 0u) {
        return NULL;
    }
    if (g_slots[h].frame != NULL && g_slots[h].frame_bytes >= n) {
        return g_slots[h].frame;
    }
    frame = pm_metal_mem_alloc((size_t)n);
    if (frame == NULL) {
        return NULL;
    }
    memset(frame, 0, (size_t)n);
    if (g_slots[h].frame != NULL) {
        size_t copy = g_slots[h].frame_bytes < n ? g_slots[h].frame_bytes : n;
        memcpy(frame, g_slots[h].frame, copy);
        pm_metal_mem_free(g_slots[h].frame);
    }
    g_slots[h].frame = frame;
    g_slots[h].frame_bytes = n;
    return frame;
}

uint32_t pm_metal_async_spawn(pm_metal_async_step_fn_t step, uint32_t state_bytes,
                              pm_metal_async_prio_t prio)
{
    uint32_t h = pm_metal_async_coro_create(step, state_bytes);

    if (h == 0u) {
        return 0;
    }
    if ((uint32_t)prio >= PM_METAL_ASYNC_N_PRIO) {
        prio = PM_METAL_ASYNC_PRIO_MED;
    }
    g_slots[h].prio = (uint8_t)prio;
    return pm_metal_async_create_task_prio(h, g_slots[h].runner, prio);
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
    if (g_slots[h].kind == SLOT_YIELD || g_slots[h].kind == SLOT_CORO
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
    (void)i;
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

    /* Quiesce checkpoint: park before any dispatch on this runner. */
    if (pm_metal_async_quiesce_requested()) {
        pm_metal_async_quiesce_park_runner(ri);
        return 0;
    }

    /* Due sleeps owned by this runner. */
    for (i = 1; i < PM_METAL_ASYNC_MAX_HANDLES; i++) {
        if (!g_slots[i].used || g_slots[i].status != PM_METAL_ASYNC_WAITING) {
            continue;
        }
        if (g_slots[i].runner != (uint8_t)ri) {
            continue;
        }
        if (g_slots[i].kind == SLOT_SLEEP && now >= g_slots[i].deadline_us) {
            /* Timing class: due sleeps complete before ready drain. */
            complete_slot(i);
            n++;
        } else if (g_slots[i].kind == SLOT_AWAIT
                   || (g_slots[i].kind == SLOT_CORO && g_slots[i].await_child != 0u)) {
            uint32_t c = g_slots[i].await_child;

            if (c == 0 || c >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[c].used) {
                g_slots[i].status = PM_METAL_ASYNC_ERROR;
                g_slots[i].await_child = 0;
                n++;
            } else if (g_slots[c].status == PM_METAL_ASYNC_DONE
                       || g_slots[c].status == PM_METAL_ASYNC_ERROR
                       || g_slots[c].status == PM_METAL_ASYNC_CANCELLED) {
                if (g_slots[i].kind == SLOT_CORO && g_slots[i].step != NULL) {
                    /* Child finished — resume step on ready ring. */
                    g_slots[i].await_child = 0;
                    g_slots[i].status = (uint8_t)PM_METAL_ASYNC_PENDING;
                    (void)ready_push(ri, i);
                    n++;
                } else {
                    g_slots[i].status = g_slots[c].status;
                    g_slots[i].result_u32 = g_slots[c].result_u32;
                    g_slots[i].await_child = 0;
                    n++;
                }
            }
        }
    }

    /* Drain ready ring (yields / create_task / coro steps). */
    steps = 0;
    while (steps < PM_METAL_ASYNC_READY_CAP) {
        uint32_t st;

        h = ready_pop(ri);
        if (h == 0u) {
            break;
        }
        steps++;
        if (h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
            continue;
        }
        if (g_slots[h].kind == SLOT_YIELD) {
            if (g_slots[h].status == PM_METAL_ASYNC_WAITING
                || g_slots[h].status == PM_METAL_ASYNC_PENDING) {
                complete_slot(h);
                n++;
            }
            continue;
        }
        if (g_slots[h].kind != SLOT_CORO || g_slots[h].step == NULL) {
            continue;
        }
        if (g_slots[h].await_child != 0u) {
            continue;
        }
        g_slots[h].status = (uint8_t)PM_METAL_ASYNC_WAITING;
#if PM_METAL_ASYNC_METER
        if (__builtin_expect(pm_metal_async_meter_on_fast() != 0, 0)) {
            uint64_t t0 = pm_metal_async_meter_cycles();
            st = g_slots[h].step(h);
            pm_metal_async_meter_record(pm_metal_async_meter_cycles() - t0);
        } else
#endif
        {
            st = g_slots[h].step(h);
        }
        if (st == (uint32_t)PM_METAL_ASYNC_DONE
            || st == (uint32_t)PM_METAL_ASYNC_ERROR
            || st == (uint32_t)PM_METAL_ASYNC_CANCELLED) {
            __atomic_store_n(&g_slots[h].status, (uint8_t)st, __ATOMIC_RELEASE);
            n++;
        } else if (st == (uint32_t)PM_METAL_ASYNC_PENDING) {
            g_slots[h].status = (uint8_t)PM_METAL_ASYNC_PENDING;
            (void)ready_push(ri, h);
            n++;
        } else {
            /* WAITING — parked on await_child or external wake. */
            g_slots[h].status = (uint8_t)PM_METAL_ASYNC_WAITING;
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

uint32_t pm_metal_async_create_task_prio(uint32_t h, uint32_t runner,
                                         pm_metal_async_prio_t prio)
{
    if (!g_started || runner >= g_n_runners) {
        return 0;
    }
    if ((uint32_t)prio >= PM_METAL_ASYNC_N_PRIO) {
        prio = PM_METAL_ASYNC_PRIO_MED;
    }
    if (h == 0u) {
        h = alloc_slot(SLOT_YIELD, 0, runner);
        if (h == 0u) {
            return 0;
        }
        g_slots[h].prio = (uint8_t)prio;
        (void)ready_push_prio(runner, h, (uint8_t)prio);
        return h;
    }
    if (h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return 0;
    }
    g_slots[h].runner = (uint8_t)runner;
    g_slots[h].prio = (uint8_t)prio;
    if (g_slots[h].kind == SLOT_YIELD || g_slots[h].kind == SLOT_CORO
        || g_slots[h].status == PM_METAL_ASYNC_PENDING) {
        (void)ready_push_prio(runner, h, (uint8_t)prio);
    }
    return h;
}

uint32_t pm_metal_async_create_task_on(uint32_t h, uint32_t runner)
{
    return pm_metal_async_create_task_prio(h, runner, PM_METAL_ASYNC_PRIO_MED);
}

volatile uint32_t pm_metal_smp_poll_ticks[PM_METAL_ASYNC_MAX_RUNNERS];

int32_t pm_metal_async_run_loop_cpu(uint32_t cpu)
{
    if (!g_started || cpu >= g_n_runners) {
        return -1;
    }
    for (;;) {
        if (pm_metal_async_quiesce_requested()) {
            pm_metal_async_quiesce_park_runner(cpu);
            PM_METAL_CPU_PAUSE();
            continue;
        }
        (void)pm_metal_async_run_poll_cpu(cpu);
        __atomic_fetch_add(&pm_metal_smp_poll_ticks[cpu], 1u, __ATOMIC_RELAXED);
        PM_METAL_CPU_PAUSE();
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

uint32_t pm_metal_async_park(void)
{
    uint32_t ri;

    if (!g_started) {
        return 0;
    }
    ri = next_runner();
    return alloc_slot(SLOT_PARK, 0, ri);
}

void pm_metal_async_wake(uint32_t h)
{
    if (h == 0 || h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[h].used) {
        return;
    }
    if (g_slots[h].kind != SLOT_PARK && g_slots[h].kind != SLOT_AWAIT) {
        return;
    }
    if (__atomic_load_n(&g_slots[h].status, __ATOMIC_ACQUIRE) != PM_METAL_ASYNC_WAITING) {
        return;
    }
    /* Wake into shared prio class (tag already on slot). */
    complete_slot(h);
}

pm_metal_async_status_t pm_metal_async_await(uint32_t self_h, uint32_t child_h)
{
    uint32_t ri;
    uint32_t ah;

    if (child_h == 0 || child_h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[child_h].used) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (self_h != 0 && self_h < PM_METAL_ASYNC_MAX_HANDLES && g_slots[self_h].used) {
        /* Keep SLOT_CORO + step so the frame can resume after the child. */
        if (g_slots[self_h].kind != SLOT_CORO) {
            g_slots[self_h].kind = SLOT_AWAIT;
        }
        g_slots[self_h].status = PM_METAL_ASYNC_WAITING;
        g_slots[self_h].await_child = child_h;
        return PM_METAL_ASYNC_WAITING;
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
