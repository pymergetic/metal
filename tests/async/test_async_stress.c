/*
 * Heavy host stress: many coros, await trees, cancel, quiesce, step meter.
 *
 *   make -C tests/async
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/meter.h>
#include <pymergetic/metal/async/quiesce.h>
#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/async/task.h>
#include <pymergetic/metal/mem.h>
#include <pymergetic/metal/process/__init__.h>

enum { HEAP_BYTES = 2u * 1024u * 1024u };
enum { N_WAVE = 48u };
enum { N_WAVES = 20u };
enum { WORK_ITERS = 64u };

static uint8_t g_heap[HEAP_BYTES] __attribute__((aligned(4096)));

typedef struct {
    uint32_t left;
    uint32_t sink;
} work_t;

static uint32_t work_step(uint32_t h)
{
    work_t *st = (work_t *)pm_metal_async_coro_state(h);
    uint32_t i;
    uint32_t acc = 0;

    if (st == NULL) {
        return (uint32_t)PM_METAL_ASYNC_ERROR;
    }
    if (st->left == 0u) {
        pm_metal_async_set_result_u32(h, st->sink);
        return (uint32_t)PM_METAL_ASYNC_DONE;
    }
    {
        volatile uint32_t *sink = &st->sink;
        for (i = 0; i < WORK_ITERS; i++) {
            acc += i * 3u + st->left;
            *sink ^= acc;
        }
    }
    st->left--;
    return (uint32_t)PM_METAL_ASYNC_PENDING;
}

static uint32_t spawn_work(uint32_t steps)
{
    uint32_t h = pm_metal_async_coro_create(work_step, sizeof(work_t));
    work_t *st;

    assert(h != 0);
    st = (work_t *)pm_metal_async_coro_state(h);
    assert(st != NULL);
    st->left = steps;
    st->sink = 0;
    assert(pm_metal_async_create_task(h) != 0);
    return h;
}

static void drain_until_done(uint32_t h, uint32_t max_polls)
{
    uint32_t i;
    for (i = 0; i < max_polls; i++) {
        (void)pm_metal_async_run_poll();
        {
            pm_metal_async_status_t st = pm_metal_async_status(h);
            if (st == PM_METAL_ASYNC_DONE || st == PM_METAL_ASYNC_ERROR
                || st == PM_METAL_ASYNC_CANCELLED) {
                return;
            }
        }
    }
    assert(0 && "timeout waiting for handle");
}

static void test_wave_throughput(void)
{
    uint32_t w, i;
    uint32_t hs[N_WAVE];
    uint64_t t0, t1;
    pm_metal_async_meter_snap_t snap;

    pm_metal_async_meter_reset();
    pm_metal_async_meter_enable(1);

    t0 = pm_metal_async_meter_cycles();
    for (w = 0; w < N_WAVES; w++) {
        for (i = 0; i < N_WAVE; i++) {
            hs[i] = spawn_work(3u + (i & 3u));
        }
        for (i = 0; i < N_WAVE; i++) {
            drain_until_done(hs[i], 100000u);
            assert(pm_metal_async_status(hs[i]) == PM_METAL_ASYNC_DONE);
            pm_metal_async_coro_close(hs[i]);
        }
    }
    t1 = pm_metal_async_meter_cycles();
    pm_metal_async_meter_snap(&snap);
    pm_metal_async_meter_enable(0);

    assert(snap.steps > 0);
    assert(snap.max_cycles >= snap.min_cycles);
    printf("wave ok steps=%llu max_cyc=%llu avg_cyc=%llu wall_cyc=%llu\n",
           (unsigned long long)snap.steps, (unsigned long long)snap.max_cycles,
           (unsigned long long)(snap.total_cycles / snap.steps),
           (unsigned long long)(t1 - t0));
}

static uint32_t child_step(uint32_t h)
{
    work_t *st = (work_t *)pm_metal_async_coro_state(h);
    if (st == NULL) {
        return (uint32_t)PM_METAL_ASYNC_ERROR;
    }
    if (st->left == 0u) {
        return (uint32_t)PM_METAL_ASYNC_DONE;
    }
    st->left--;
    return (uint32_t)PM_METAL_ASYNC_PENDING;
}

typedef struct {
    uint32_t child;
    uint8_t phase;
} parent_t;

static uint32_t parent_step(uint32_t h)
{
    parent_t *st = (parent_t *)pm_metal_async_coro_state(h);
    if (st == NULL) {
        return (uint32_t)PM_METAL_ASYNC_ERROR;
    }
    if (st->phase == 0) {
        uint32_t c = pm_metal_async_coro_create(child_step, sizeof(work_t));
        work_t *cs = (work_t *)pm_metal_async_coro_state(c);
        assert(c && cs);
        cs->left = 5;
        cs->sink = 0;
        st->child = c;
        (void)pm_metal_async_create_task(c);
        (void)pm_metal_async_await(h, c);
        st->phase = 1;
        return (uint32_t)PM_METAL_ASYNC_WAITING;
    }
    return (uint32_t)PM_METAL_ASYNC_DONE;
}

static void test_await_cancel_tree(void)
{
    uint32_t p = pm_metal_async_coro_create(parent_step, sizeof(parent_t));
    parent_t *st = (parent_t *)pm_metal_async_coro_state(p);
    uint32_t child;
    assert(p && st);
    st->child = 0;
    st->phase = 0;
    assert(pm_metal_async_create_task(p) != 0);

    {
        uint32_t i;
        for (i = 0; i < 1000u && st->child == 0; i++) {
            (void)pm_metal_async_run_poll();
        }
    }
    assert(st->child != 0);
    child = st->child;

    pm_metal_async_cancel_tree(p);
    /* Slots closed — handles must not still be live. */
    assert(pm_metal_async_coro_state(p) == NULL);
    assert(pm_metal_async_coro_state(child) == NULL);
    puts("await/cancel ok");
}

static void test_quiesce(void)
{
    uint32_t h = spawn_work(1000u);
    pm_metal_async_quiesce_request();
    {
        uint32_t i;
        for (i = 0; i < 100u; i++) {
            (void)pm_metal_async_run_poll();
            if (pm_metal_async_quiesce_all_parked()) {
                break;
            }
        }
    }
    assert(pm_metal_async_quiesce_all_parked());
    pm_metal_async_quiesce_release();
    drain_until_done(h, 200000u);
    pm_metal_async_coro_close(h);
    puts("quiesce ok");
}

static int g_td_hits;
static void stress_td(uint32_t pid, void *user)
{
    (void)pid;
    (void)user;
    g_td_hits++;
}

static void test_process_teardown(void)
{
    uint32_t h = spawn_work(50u);
    uint32_t pid = pm_metal_process_crown(h, PM_METAL_PROCESS_MODE_DAEMON, "stressd", stress_td, NULL);
    assert(pid != 0);
    assert(pm_metal_process_quit(pid, 0) == 0);
    assert(g_td_hits == 1);
    puts("process teardown ok");
}

int main(void)
{
    assert(pm_metal_mem_init(g_heap, sizeof(g_heap)) == 0);
    assert(pm_metal_async_start(1) == 0);

    test_wave_throughput();
    test_await_cancel_tree();
    test_quiesce();
    test_process_teardown();

    puts("async stress ok");
    return 0;
}
