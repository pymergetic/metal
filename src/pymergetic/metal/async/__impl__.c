/* pymergetic.metal.async — stackless runner (new; not a port of run.c/coro.c).
 *
 * Ready ring: CAS per slot, no scheduler mutex.
 * Timer list: sorted deadlines; fire = ring push.
 * ip pump: weak pm_metal_net_ip_pump until net.ip is strong.
 * Firmware: rdtsc clock, no POSIX poll/nanosleep.
 */
#if !defined(PM_METAL_FIRMWARE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "pymergetic/metal/async/__exports__.h"

#include "pymergetic/util/mem.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#if !defined(PM_METAL_FIRMWARE)
#include <time.h>
#if defined(__linux__) || defined(__APPLE__)
#include <poll.h>
#define PM_METAL_ASYNC_HAVE_POLL 1
#endif
#endif

#ifndef PM_METAL_ASYNC_RING_N
#define PM_METAL_ASYNC_RING_N 64u
#endif

#define PM_METAL_RING_ST_EMPTY 0ull
#define PM_METAL_RING_ST_READY 1ull
#define PM_METAL_RING_ST_CLAIMED 2ull
#define PM_METAL_RING_KIND_TASK 1ull
#define PM_METAL_RING_KIND_STOP 3ull
#define PM_METAL_RING_PAY_MASK 0x0000ffffffffffffull

struct pm_metal_async_timer {
    struct pm_metal_async_timer *next;
    uint64_t deadline_us;
    pm_metal_async_task_t *task;
};

typedef struct {
    _Alignas(8) atomic_uint_least64_t slot[PM_METAL_ASYNC_RING_N];
    uint32_t head;
    uint32_t tail;
} pm_metal_async_inbox_t;

static pm_util_mem_arena_t *s_arena;
static pm_metal_async_inbox_t *s_inbox;
static pm_metal_async_task_t *s_current;
static struct pm_metal_async_timer *s_timers;
static uint32_t s_ready;

static uint64_t ring_pack(uint64_t st, uint64_t kind, uint64_t pay) {
    return (st << 62) | (kind << 48) | (pay & PM_METAL_RING_PAY_MASK);
}

static uint64_t ring_st(uint64_t w) {
    return w >> 62;
}

static uint64_t ring_kind(uint64_t w) {
    return (w >> 48) & 0x3fffull;
}

static uint64_t ring_pay(uint64_t w) {
    return w & PM_METAL_RING_PAY_MASK;
}

static uint64_t ptr_pay(void *p) {
    return (uint64_t)(uintptr_t)p & PM_METAL_RING_PAY_MASK;
}

static void *pay_ptr(uint64_t pay) {
    return (void *)(uintptr_t)pay;
}

__attribute__((weak)) void pm_metal_net_ip_pump(void) {
}

__attribute__((weak)) int32_t pm_metal_drivers_net_tap_fd(void) {
    return -1;
}

uint64_t pm_metal_async_mono_us(void) {
#if defined(PM_METAL_FIRMWARE)
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (((uint64_t)hi << 32) | (uint64_t)lo) / 2000ull;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000ull) + ((uint64_t)ts.tv_nsec / 1000ull);
#endif
}

static void idle_wait_us(uint64_t us) {
#if defined(PM_METAL_FIRMWARE)
    uint64_t t0;
    if (us == 0) {
        return;
    }
    t0 = pm_metal_async_mono_us();
    while (pm_metal_async_mono_us() - t0 < us) {
        __asm__ volatile("pause");
    }
#else
    int32_t fd;
#if defined(PM_METAL_ASYNC_HAVE_POLL)
    struct pollfd pfd;
    int timeout_ms;
#endif
    if (us == 0) {
        return;
    }
    fd = pm_metal_drivers_net_tap_fd();
#if defined(PM_METAL_ASYNC_HAVE_POLL)
    if (fd >= 0) {
        pfd.fd = (int)fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        timeout_ms = (int)((us + 999ull) / 1000ull);
        if (timeout_ms < 1) {
            timeout_ms = 1;
        }
        if (timeout_ms > 1000000) {
            timeout_ms = 1000000;
        }
        (void)poll(&pfd, 1, timeout_ms);
        return;
    }
#endif
    {
        struct timespec ts;
        ts.tv_sec = (time_t)(us / 1000000ull);
        ts.tv_nsec = (long)((us % 1000000ull) * 1000ull);
        (void)clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
        (void)fd;
    }
#endif
}

static int32_t ring_push(uint64_t kind, void *payload) {
    pm_metal_async_inbox_t *in = s_inbox;
    uint64_t neu = ring_pack(PM_METAL_RING_ST_READY, kind, ptr_pay(payload));
    uint32_t i;
    for (i = 0; i < PM_METAL_ASYNC_RING_N; i++) {
        uint32_t idx = (in->tail + i) % PM_METAL_ASYNC_RING_N;
        uint_least64_t exp = 0;
        if (atomic_compare_exchange_strong(&in->slot[idx], &exp, (uint_least64_t)neu)) {
            in->tail = (idx + 1u) % PM_METAL_ASYNC_RING_N;
            return 0;
        }
    }
    return -1;
}

static int32_t ring_claim(uint64_t *word_out) {
    pm_metal_async_inbox_t *in = s_inbox;
    uint32_t i;
    for (i = 0; i < PM_METAL_ASYNC_RING_N; i++) {
        uint32_t idx = (in->head + i) % PM_METAL_ASYNC_RING_N;
        uint_least64_t word = atomic_load(&in->slot[idx]);
        if (ring_st((uint64_t)word) != PM_METAL_RING_ST_READY) {
            continue;
        }
        if (ring_kind((uint64_t)word) == PM_METAL_RING_KIND_STOP) {
            continue;
        }
        uint_least64_t claimed = (uint_least64_t)ring_pack(
            PM_METAL_RING_ST_CLAIMED, ring_kind((uint64_t)word), ring_pay((uint64_t)word));
        if (atomic_compare_exchange_strong(&in->slot[idx], &word, claimed)) {
            in->head = (idx + 1u) % PM_METAL_ASYNC_RING_N;
            *word_out = (uint64_t)claimed;
            return 0;
        }
    }
    return -1;
}

static void ring_release_word(uint64_t claimed) {
    pm_metal_async_inbox_t *in = s_inbox;
    uint32_t i;
    for (i = 0; i < PM_METAL_ASYNC_RING_N; i++) {
        uint_least64_t w = atomic_load(&in->slot[i]);
        if (w == (uint_least64_t)claimed) {
            atomic_store(&in->slot[i], 0);
            return;
        }
    }
}

static pm_metal_async_task_t *task_of(pm_metal_async_coro_t *c) {
    while (c != NULL) {
        if (c->task != NULL) {
            return c->task;
        }
        c = c->waiter;
    }
    return NULL;
}

static void timer_insert(struct pm_metal_async_timer *tm) {
    struct pm_metal_async_timer **pp = &s_timers;
    while (*pp != NULL && (*pp)->deadline_us <= tm->deadline_us) {
        pp = &(*pp)->next;
    }
    tm->next = *pp;
    *pp = tm;
}

static void fire_timers(void) {
    uint64_t now = pm_metal_async_mono_us();
    while (s_timers != NULL && s_timers->deadline_us <= now) {
        struct pm_metal_async_timer *tm = s_timers;
        s_timers = tm->next;
        (void)ring_push(PM_METAL_RING_KIND_TASK, tm->task);
        pm_util_mem_free(s_arena, tm);
    }
}

static uint64_t next_timer_deadline(void) {
    if (s_timers == NULL) {
        return UINT64_MAX;
    }
    return s_timers->deadline_us;
}

static void step_task(pm_metal_async_task_t *task) {
    if (task == NULL || task->root == NULL || task->on_c_stack) {
        return;
    }
    s_current = task;
    for (;;) {
        pm_metal_async_coro_t *leaf = task->root;
        while (leaf->awaiting != NULL) {
            leaf = leaf->awaiting;
        }
        if (leaf->step == NULL) {
            leaf->status = PM_METAL_ASYNC_ERROR;
            break;
        }
        pm_metal_async_status_t st = leaf->step(leaf);
        leaf->status = (uint32_t)st;
        if (st == PM_METAL_ASYNC_WAITING) {
            if (leaf->awaiting != NULL) {
                continue;
            }
            break;
        }
        if (st == PM_METAL_ASYNC_DONE && leaf->waiter != NULL) {
            leaf->waiter->awaiting = NULL;
            continue;
        }
        break;
    }
    s_current = NULL;
}

static void drain_one(void) {
    uint64_t word;
    if (ring_claim(&word) != 0) {
        return;
    }
    pm_metal_async_task_t *task = (pm_metal_async_task_t *)pay_ptr(ring_pay(word));
    ring_release_word(word);
    if (task == s_current) {
        (void)ring_push(PM_METAL_RING_KIND_TASK, task);
        return;
    }
    step_task(task);
}

int32_t pm_metal_async_init(pm_util_mem_arena_t *arena, uint32_t ncpu) {
    if (arena == NULL || ncpu != 1u) {
        return -1;
    }
    if (s_ready) {
        return 0;
    }
    s_arena = arena;
    s_inbox = (pm_metal_async_inbox_t *)pm_util_mem_alloc(arena, sizeof(*s_inbox));
    if (s_inbox == NULL) {
        s_arena = NULL;
        return -1;
    }
    memset(s_inbox, 0, sizeof(*s_inbox));
    s_current = NULL;
    s_timers = NULL;
    s_ready = 1;
    return 0;
}

void pm_metal_async_deinit(void) {
    while (s_timers != NULL) {
        struct pm_metal_async_timer *tm = s_timers;
        s_timers = tm->next;
        if (s_arena != NULL) {
            pm_util_mem_free(s_arena, tm);
        }
    }
    s_inbox = NULL;
    s_arena = NULL;
    s_current = NULL;
    s_ready = 0;
}

pm_metal_async_coro_t *pm_metal_async_coro_create(pm_metal_async_step_fn step, size_t frame_bytes) {
    if (!s_ready || step == NULL || frame_bytes < sizeof(pm_metal_async_coro_t)) {
        return NULL;
    }
    pm_metal_async_coro_t *c = (pm_metal_async_coro_t *)pm_util_mem_alloc(s_arena, frame_bytes);
    if (c == NULL) {
        return NULL;
    }
    memset(c, 0, frame_bytes);
    c->step = step;
    c->status = PM_METAL_ASYNC_PENDING;
    return c;
}

pm_metal_async_task_t *pm_metal_async_create_task(pm_metal_async_coro_t *coro) {
    if (!s_ready || coro == NULL) {
        return NULL;
    }
    pm_metal_async_task_t *t = (pm_metal_async_task_t *)pm_util_mem_alloc(s_arena, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    memset(t, 0, sizeof(*t));
    t->root = coro;
    coro->task = t;
    if (ring_push(PM_METAL_RING_KIND_TASK, t) != 0) {
        return NULL;
    }
    return t;
}

pm_metal_async_status_t pm_metal_async_await(pm_metal_async_coro_t *self, pm_metal_async_coro_t *child) {
    if (self == NULL || child == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (self->awaiting != NULL && self->awaiting != child) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (child->status == PM_METAL_ASYNC_DONE) {
        self->awaiting = NULL;
        return PM_METAL_ASYNC_DONE;
    }
    self->awaiting = child;
    child->waiter = self;
    if (child->task == NULL) {
        child->task = task_of(self);
    }
    self->status = PM_METAL_ASYNC_WAITING;
    return PM_METAL_ASYNC_WAITING;
}

pm_metal_async_status_t pm_metal_async_yield_park(pm_metal_async_coro_t *self) {
    pm_metal_async_task_t *t = task_of(self);
    if (t == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    self->status = PM_METAL_ASYNC_WAITING;
    if (ring_push(PM_METAL_RING_KIND_TASK, t) != 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    return PM_METAL_ASYNC_WAITING;
}

pm_metal_async_status_t pm_metal_async_sleep_us(pm_metal_async_coro_t *self, uint64_t us) {
    pm_metal_async_task_t *t = task_of(self);
    if (t == NULL || !s_ready) {
        return PM_METAL_ASYNC_ERROR;
    }
    struct pm_metal_async_timer *tm =
        (struct pm_metal_async_timer *)pm_util_mem_alloc(s_arena, sizeof(*tm));
    if (tm == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    tm->next = NULL;
    tm->deadline_us = pm_metal_async_mono_us() + us;
    tm->task = t;
    timer_insert(tm);
    self->status = PM_METAL_ASYNC_WAITING;
    return PM_METAL_ASYNC_WAITING;
}

uint32_t pm_metal_async_yield(void) {
    if (!s_ready) {
        return 0;
    }
    pm_metal_net_ip_pump();
    fire_timers();
    unsigned n;
    for (n = 0; n < 8u; n++) {
        drain_one();
        pm_metal_net_ip_pump();
    }
    return 0;
}

void pm_metal_async_poll(void) {
    if (!s_ready) {
        return;
    }
    pm_metal_net_ip_pump();
    fire_timers();
    unsigned n;
    for (n = 0; n < PM_METAL_ASYNC_RING_N; n++) {
        uint64_t word;
        if (ring_claim(&word) != 0) {
            break;
        }
        pm_metal_async_task_t *task = (pm_metal_async_task_t *)pay_ptr(ring_pay(word));
        ring_release_word(word);
        step_task(task);
        pm_metal_net_ip_pump();
        fire_timers();
    }
}

int32_t pm_metal_async_run_until(pm_metal_async_coro_t *waiter) {
    if (!s_ready || waiter == NULL) {
        return -1;
    }
    pm_metal_async_task_t *owner = s_current;
    if (owner != NULL) {
        owner->on_c_stack = 1;
    }
    unsigned empty_rounds = 0;
    while (waiter->status != PM_METAL_ASYNC_DONE && waiter->status != PM_METAL_ASYNC_ERROR
        && waiter->status != PM_METAL_ASYNC_CANCELLED) {
        pm_metal_net_ip_pump();
        fire_timers();
        uint64_t word;
        if (ring_claim(&word) == 0) {
            pm_metal_async_task_t *task = (pm_metal_async_task_t *)pay_ptr(ring_pay(word));
            ring_release_word(word);
            empty_rounds = 0;
            if (task == owner) {
                continue;
            }
            step_task(task);
            continue;
        }
        uint64_t next = next_timer_deadline();
        if (next == UINT64_MAX) {
            empty_rounds++;
            if (empty_rounds > 1u) {
                if (owner != NULL) {
                    owner->on_c_stack = 0;
                }
                return -1;
            }
            continue;
        }
        uint64_t now = pm_metal_async_mono_us();
        uint64_t wait = next > now ? next - now : 0;
        if (wait > 1000000ull) {
            wait = 1000000ull;
        }
        idle_wait_us(wait);
        empty_rounds = 0;
    }
    if (owner != NULL) {
        owner->on_c_stack = 0;
    }
    return waiter->status == PM_METAL_ASYNC_DONE ? 0 : -1;
}

int32_t pm_metal_async_run(pm_metal_async_task_t *task) {
    if (task == NULL || task->root == NULL) {
        return -1;
    }
    return pm_metal_async_run_until(task->root);
}

pm_metal_async_task_t *pm_metal_async_current_task(void) {
    return s_current;
}

int32_t pm_metal_async_post_task(pm_metal_async_task_t *task) {
    if (!s_ready || task == NULL) {
        return -1;
    }
    return ring_push(PM_METAL_RING_KIND_TASK, task);
}

static int32_t pm_metal_async_boot(pm_util_mem_arena_t *arena) {
    return pm_metal_async_init(arena, 1u);
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_init, pm_metal_async_init, int32_t(pm_util_mem_arena_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_deinit, pm_metal_async_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_coro_create, pm_metal_async_coro_create, pm_metal_async_coro_t *(pm_metal_async_step_fn, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_create_task, pm_metal_async_create_task, pm_metal_async_task_t *(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_await, pm_metal_async_await, pm_metal_async_status_t(pm_metal_async_coro_t *, pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_yield_park, pm_metal_async_yield_park, pm_metal_async_status_t(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_sleep_us, pm_metal_async_sleep_us, pm_metal_async_status_t(pm_metal_async_coro_t *, uint64_t));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_yield, pm_metal_async_yield, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_poll, pm_metal_async_poll, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_run_until, pm_metal_async_run_until, int32_t(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_run, pm_metal_async_run, int32_t(pm_metal_async_task_t *));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_mono_us, pm_metal_async_mono_us, uint64_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_current_task, pm_metal_async_current_task, pm_metal_async_task_t *(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_post_task, pm_metal_async_post_task, int32_t(pm_metal_async_task_t *));

PM_MOD_BOOT_C(pymergetic.metal.async, pm_metal_async_boot, pm_metal_async_deinit);
