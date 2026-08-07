#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/board_time.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_ASYNC_MAX_HANDLES
#define PM_METAL_ASYNC_MAX_HANDLES 64
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
    uint64_t deadline_us;
    uint32_t await_child;
    uint32_t result_u32;
} slot_t;

static slot_t g_slots[PM_METAL_ASYNC_MAX_HANDLES];
static uint32_t g_n_runners;
static int g_started;
static uint64_t g_boot_us;

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

static uint32_t alloc_slot(slot_kind_t kind, uint64_t deadline_us)
{
    uint32_t i;
    for (i = 1; i < PM_METAL_ASYNC_MAX_HANDLES; i++) {
        if (!g_slots[i].used) {
            g_slots[i].used = 1;
            g_slots[i].kind = (uint8_t)kind;
            g_slots[i].status = PM_METAL_ASYNC_WAITING;
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
    return (pm_metal_async_status_t)g_slots[h].status;
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
    return alloc_slot(SLOT_SLEEP, pm_metal_async_mono_us() + us);
}

uint32_t pm_metal_async_sleep_until_us(uint64_t deadline_us)
{
    return alloc_slot(SLOT_SLEEP, deadline_us);
}

uint32_t pm_metal_async_sleep(uint32_t ms)
{
    return pm_metal_async_sleep_us((uint64_t)ms * 1000ull);
}

uint32_t pm_metal_async_yield(void)
{
    return alloc_slot(SLOT_YIELD, 0);
}

int32_t pm_metal_async_start(uint32_t n_cpus)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_n_runners = n_cpus == 0 ? 1u : n_cpus;
    g_started = 1;
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
    g_slots[i].status = PM_METAL_ASYNC_DONE;
}

int32_t pm_metal_async_run_poll(void)
{
    uint64_t now;
    uint32_t i;
    int32_t n = 0;

    if (!g_started) {
        return -1;
    }
    now = pm_metal_async_mono_us();
    for (i = 1; i < PM_METAL_ASYNC_MAX_HANDLES; i++) {
        if (!g_slots[i].used || g_slots[i].status != PM_METAL_ASYNC_WAITING) {
            continue;
        }
        if (g_slots[i].kind == SLOT_SLEEP) {
            if (now >= g_slots[i].deadline_us) {
                complete_slot(i);
                n++;
            }
        } else if (g_slots[i].kind == SLOT_YIELD) {
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
    return n;
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

pm_metal_async_status_t pm_metal_async_await(uint32_t self_h, uint32_t child_h)
{
    (void)self_h;
    if (child_h == 0 || child_h >= PM_METAL_ASYNC_MAX_HANDLES || !g_slots[child_h].used) {
        return PM_METAL_ASYNC_ERROR;
    }
    (void)pm_metal_async_run_poll();
    return pm_metal_async_status(child_h);
}
