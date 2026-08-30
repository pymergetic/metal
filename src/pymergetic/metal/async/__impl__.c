/* pymergetic.metal.async — stackless runner (new; not a port of run.c/coro.c).
 *
 * Ready ring: CAS per slot, no scheduler mutex. Slot table is alloc()'d from
 * the arena heap at init (try 1M slots / 8 MiB, shrink until it fits).
 * Timer list: sorted deadlines; fire = ring push.
 * ip pump: weak pm_metal_net_ip_pump until net.ip is strong.
 * Firmware: rdtsc clock, no POSIX poll/nanosleep.
 */
#if !defined(PM_METAL_FIRMWARE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "pymergetic/metal/async/__exports__.h"

#include "pymergetic/util/lock.h"
#include "pymergetic/util/mem.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(PM_METAL_FIRMWARE)
#include <time.h>
#if defined(__linux__) || defined(__APPLE__)
#include <poll.h>
#define PM_METAL_ASYNC_HAVE_POLL 1
#endif
#if !defined(__EMSCRIPTEN__)
#include <pthread.h>
#define PM_METAL_ASYNC_PTHREAD 1
#endif
#endif

#define PM_METAL_RING_ST_EMPTY 0ull
#define PM_METAL_RING_ST_READY 1ull
#define PM_METAL_RING_ST_CLAIMED 2ull
#define PM_METAL_RING_KIND_TASK 1ull
#define PM_METAL_RING_KIND_STOP 3ull
#define PM_METAL_RING_PAY_MASK 0x0000ffffffffffffull
/* Ready-queue start size (slots). Each slot is 8 bytes; alloc() from the
 * arena heap, then shrink until it fits. Not a compile-time cap on tasks. */
#define PM_METAL_ASYNC_RING_WANT (1u << 20)
#define PM_METAL_ASYNC_RING_MIN 256u
/* xAPIC CPUID leaf 1 id is 8-bit. Lookup width, not a core cap. */
#define PM_METAL_ASYNC_APIC_N 256u
/* How long run_until keeps waiting with nothing runnable before it calls the
 * wait dead. Must survive one guest-TCP retransmit cycle: a dropped packet
 * on a real wire (QEMU user-net under load drops) waits out the initial RTO
 * (~200-300ms) with nothing runnable, and firing first turns a healthy
 * fetch into "fetch failed". 1s tolerates a retransmit and still catches a
 * genuinely dead wait far below any human timeout. */
#define PM_METAL_ASYNC_STALL_US 1000000ull

struct pm_metal_async_timer {
    struct pm_metal_async_timer *next;
    uint64_t deadline_us;
    pm_metal_async_task_t *task;
};

typedef struct {
    uint32_t n;
    uint32_t mask;
    atomic_uint head;
    atomic_uint tail;
    atomic_uint count;
    _Alignas(8) atomic_uint_least64_t slot[];
} pm_metal_async_inbox_t;

static pm_util_mem_arena_t *s_arena;
static pm_metal_async_inbox_t *s_inbox;
static struct pm_metal_async_timer *s_timers;
static pm_util_lock_t s_timer_lock;
/* VM-entry mutex: async-aware (park on contention, wake on release, never spin).
 * The interpreter is one resource shared by every runner core; a vm_only coro
 * takes this mutex for the duration of its step. Replaces the ad-hoc
 * vm_enter/vm_leave hand-back on raw s_vm_lock. */
static pm_metal_async_mutex_t s_vm_mutex;
static uint32_t s_vm_lock_ready;
static uint32_t s_ready;
static uint32_t s_ncpu;
/* Worker (AP / runner pthread) marking per current-slot. Kept for CPU id / slot
 * bookkeeping; it no longer gates vm_only stepping. A plain slot array keeps
 * TLS out of firmware. */
static uint32_t s_worker[PM_METAL_ASYNC_APIC_N];
/* Per-slot: 1 if that runner installed MicroPython thread state and may re-enter
 * the bytecode VM. The boot thread (s_worker == 0) is always VM-capable. */
static uint32_t s_vm_capable[PM_METAL_ASYNC_APIC_N];
#if defined(PM_METAL_ASYNC_PTHREAD)
static uint32_t s_njoin;
static pthread_t *s_thread;
static __thread uint32_t s_cpu;
static __thread pm_metal_async_task_t *s_current;
#else
static pm_metal_async_task_t *s_current_cpu[PM_METAL_ASYNC_APIC_N];
static uint32_t s_ncurrent = PM_METAL_ASYNC_APIC_N;
#endif
static atomic_uint s_alive;
static atomic_uint s_run;
static atomic_uint s_busy;

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

#if defined(PM_METAL_FIRMWARE)
#include "pm_cpu.h"
#endif

__attribute__((weak)) uint32_t pm_metal_async_fill_ncpu(void) {
#if defined(PM_METAL_ASYNC_PTHREAD)
    return 4u;
#else
    return 1u;
#endif
}

/* Seat hook: called once when a runner core starts, passing its slot. A seat that
 * threads MicroPython (upy) overrides this to install per-runner MP thread state so
 * any core may step a vm_only task; it returns 1 iff the runner is then safe to
 * re-enter the bytecode VM. Default returns 0, so on a seat that cannot bring that
 * up (e.g. firmware pre-thread) a runner will never consume the interpreter. The
 * async card itself never depends on MicroPython. */
__attribute__((weak)) int pm_metal_async_runner_begin(uint32_t slot) {
    (void)slot;
    return 0;
}

#if !defined(PM_METAL_ASYNC_PTHREAD)
__attribute__((weak)) int32_t pm_metal_async_fill_start_aps(pm_util_mem_arena_t *arena, uint32_t ncpu,
    void (*entry)(void *)) {
    (void)arena;
    (void)ncpu;
    (void)entry;
    return -1;
}
#endif

#if !defined(PM_METAL_ASYNC_PTHREAD)
static uint32_t cpu_id(void) {
#if defined(PM_METAL_FIRMWARE) && (defined(__x86_64__) || defined(__i386__))
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    b = (b >> 24) & 0xffu;
    if (b >= s_ncurrent) {
        return 0;
    }
    return b;
#else
    return 0;
#endif
}
#endif

static pm_metal_async_task_t **current_slot(void) {
#if defined(PM_METAL_ASYNC_PTHREAD)
    return &s_current;
#else
    uint32_t id = cpu_id();
    if (id >= s_ncurrent) {
        id = 0;
    }
    return &s_current_cpu[id];
#endif
}

static uint32_t cpu_slot(void) {
#if defined(PM_METAL_ASYNC_PTHREAD)
    return s_cpu;
#else
    uint32_t id = cpu_id();
    if (id >= PM_METAL_ASYNC_APIC_N) {
        id = 0;
    }
    return id;
#endif
}

uint64_t pm_metal_async_mono_us(void) {
#if defined(PM_METAL_FIRMWARE)
    return pm_cpu_mono_us();
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
        pm_cpu_pause();
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

static size_t inbox_bytes(uint32_t n) {
    return sizeof(pm_metal_async_inbox_t) + (size_t)n * sizeof(atomic_uint_least64_t);
}

static pm_metal_async_inbox_t *inbox_map(pm_util_mem_arena_t *arena) {
    uint32_t n = PM_METAL_ASYNC_RING_WANT;
    while (n >= PM_METAL_ASYNC_RING_MIN) {
        size_t bytes = inbox_bytes(n);
        pm_metal_async_inbox_t *in = (pm_metal_async_inbox_t *)pm_util_mem_alloc(arena, bytes);
        if (in != NULL) {
            memset(in, 0, bytes);
            in->n = n;
            in->mask = n - 1u;
            return in;
        }
        n >>= 1;
    }
    return NULL;
}

static int32_t ring_push(uint64_t kind, void *payload) {
    pm_metal_async_inbox_t *in = s_inbox;
    uint64_t neu = ring_pack(PM_METAL_RING_ST_READY, kind, ptr_pay(payload));
    uint32_t n;
    uint32_t mask;
    uint32_t start;
    uint32_t hunt;
    uint32_t i;
    if (in == NULL) {
        return -1;
    }
    n = in->n;
    mask = in->mask;
    if (atomic_load(&in->count) >= n) {
        return -1;
    }
    start = atomic_load(&in->tail);
    hunt = n - atomic_load(&in->count);
    if (hunt > n) {
        hunt = n;
    }
    hunt += 16u;
    if (hunt > n) {
        hunt = n;
    }
    for (i = 0; i < hunt; i++) {
        uint32_t idx = (start + i) & mask;
        uint_least64_t exp = 0;
        if (atomic_compare_exchange_strong(&in->slot[idx], &exp, (uint_least64_t)neu)) {
            atomic_store(&in->tail, (idx + 1u) & mask);
            atomic_fetch_add(&in->count, 1u);
            return 0;
        }
    }
    return -1;
}

static int32_t ring_claim(uint64_t *word_out, uint32_t *idx_out) {
    pm_metal_async_inbox_t *in = s_inbox;
    uint32_t n;
    uint32_t mask;
    uint32_t start;
    uint32_t hunt;
    uint32_t i;
    if (in == NULL || atomic_load(&in->count) == 0u) {
        return -1;
    }
    n = in->n;
    mask = in->mask;
    start = atomic_load(&in->head);
    hunt = atomic_load(&in->count) + 16u;
    if (hunt > n) {
        hunt = n;
    }
    for (i = 0; i < hunt; i++) {
        uint32_t idx = (start + i) & mask;
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
            atomic_store(&in->head, (idx + 1u) & mask);
            *word_out = (uint64_t)claimed;
            *idx_out = idx;
            return 0;
        }
    }
    return -1;
}

static void ring_release(uint32_t idx) {
    pm_metal_async_inbox_t *in = s_inbox;
    if (in == NULL || idx >= in->n) {
        return;
    }
    atomic_store(&in->slot[idx], 0);
    atomic_fetch_sub(&in->count, 1u);
}

static int ring_empty(void) {
    return s_inbox == NULL || atomic_load(&s_inbox->count) == 0u;
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
    struct pm_metal_async_timer **pp;
    pm_util_lock_acquire(&s_timer_lock);
    pp = &s_timers;
    while (*pp != NULL && (*pp)->deadline_us <= tm->deadline_us) {
        pp = &(*pp)->next;
    }
    tm->next = *pp;
    *pp = tm;
    pm_util_lock_release(&s_timer_lock);
}

static void fire_timers(void) {
    uint64_t now = pm_metal_async_mono_us();
    pm_util_lock_acquire(&s_timer_lock);
    while (s_timers != NULL && s_timers->deadline_us <= now) {
        struct pm_metal_async_timer *tm = s_timers;
        s_timers = tm->next;
        pm_util_lock_release(&s_timer_lock);
        (void)ring_push(PM_METAL_RING_KIND_TASK, tm->task);
        pm_util_mem_free(s_arena, tm);
        pm_util_lock_acquire(&s_timer_lock);
        now = pm_metal_async_mono_us();
    }
    pm_util_lock_release(&s_timer_lock);
}

static uint64_t next_timer_deadline(void) {
    uint64_t d;
    pm_util_lock_acquire(&s_timer_lock);
    d = s_timers == NULL ? UINT64_MAX : s_timers->deadline_us;
    pm_util_lock_release(&s_timer_lock);
    return d;
}

void pm_metal_async_coro_set_vm_only(pm_metal_async_coro_t *coro) {
    if (coro != NULL) {
        coro->vm_only = 1u;
    }
}

/* ===== Async mutex: one reusable cast (park-on-contention, wake-on-release). ===== */

void pm_metal_async_mutex_init(pm_metal_async_mutex_t *m) {
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof(*m));
    pm_util_lock_init(&m->fifo_lock);
}

/* Try to claim the mutex. On success (owner: NULL→task CAS passes) returns
 * PM_METAL_ASYNC_PENDING — the caller must then step the coro. On contention
 * the current task is parked on the FIFO and PM_METAL_ASYNC_WAITING is
 * returned; the runner hands the task back to the ready ring.
 *
 * Race window between CAS fail and push-to-FIFO: the holder may release and
 * pop the FIFO between our fail and our push. The fifo_lock serializes that
 * window — under the lock, re-check owner after the push; if owner is NULL
 * now (release popped nothing), self-claim and return PENDING. */
pm_metal_async_status_t pm_metal_async_mutex_try_acquire(pm_metal_async_mutex_t *m, pm_metal_async_coro_t *self) {
    pm_metal_async_task_t *task;
    pm_metal_async_task_t *expected = NULL;
    if (m == NULL || self == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    task = task_of(self);
    if (task == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    /* Re-entry after a wake: the mutex release transferred ownership to this
     * task already. Proceed without any CAS. */
    if (m->owner == task) {
        return PM_METAL_ASYNC_PENDING;
    }
    if (__atomic_compare_exchange_n(&m->owner, &expected, task, 0, __ATOMIC_ACQ_REL,
            __ATOMIC_RELAXED)) {
        return PM_METAL_ASYNC_PENDING;
    }
    /* Contended: park this task on the mutex FIFO under the fifo_lock, then
     * re-check — if owner changed to NULL between our CAS fail and this push,
     * self-claim and proceed without waiting. */
    pm_util_lock_acquire(&m->fifo_lock);
    task->mutex_next = NULL;
    if (m->waiters_tail != NULL) {
        m->waiters_tail->mutex_next = task;
    } else {
        m->waiters_head = task;
    }
    m->waiters_tail = task;
    /* Re-check: the holder may have released and popped nothing. */
    expected = NULL;
    if (__atomic_compare_exchange_n(&m->owner, &expected, task, 0, __ATOMIC_ACQ_REL,
            __ATOMIC_RELAXED)) {
        /* Self-claimed after re-check: dequeue self from FIFO. */
        pm_metal_async_task_t *prev = NULL;
        pm_metal_async_task_t *cur = m->waiters_head;
        while (cur != NULL && cur != task) {
            prev = cur;
            cur = cur->mutex_next;
        }
        if (cur == task) {
            if (prev != NULL) {
                prev->mutex_next = task->mutex_next;
            } else {
                m->waiters_head = task->mutex_next;
            }
            if (m->waiters_tail == task) {
                m->waiters_tail = prev;
            }
        }
        task->mutex_next = NULL;
        pm_util_lock_release(&m->fifo_lock);
        return PM_METAL_ASYNC_PENDING;
    }
    pm_util_lock_release(&m->fifo_lock);
    return PM_METAL_ASYNC_WAITING;
}

/* Release the mutex. If a task is waiting, transfer ownership directly (the
 * woken task becomes m->owner) and push it to the ready ring. Under
 * fifo_lock the pop is atomic with respect to the contending try_acquire
 * re-check, so no task is lost. */
void pm_metal_async_mutex_release(pm_metal_async_mutex_t *m) {
    pm_metal_async_task_t *wake;
    if (m == NULL) {
        return;
    }
    pm_util_lock_acquire(&m->fifo_lock);
    wake = m->waiters_head;
    if (wake != NULL) {
        m->waiters_head = wake->mutex_next;
        if (m->waiters_head == NULL) {
            m->waiters_tail = NULL;
        }
        wake->mutex_next = NULL;
    }
    /* Transfer or clear ownership under fifo_lock: a racing try_acquire that
     * pushed to FIFO and is about to re-check will see the correct state. */
    __atomic_store_n(&m->owner, wake, __ATOMIC_RELEASE);
    pm_util_lock_release(&m->fifo_lock);
    if (wake != NULL) {
        (void)ring_push(PM_METAL_RING_KIND_TASK, wake);
    }
}

static void step_task(pm_metal_async_task_t *task) {
    pm_metal_async_task_t **cur;
    if (task == NULL || task->root == NULL || task->on_c_stack) {
        return;
    }
    if (task->root->status == PM_METAL_ASYNC_DONE || task->root->status == PM_METAL_ASYNC_ERROR
        || task->root->status == PM_METAL_ASYNC_CANCELLED) {
        return;
    }
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&task->running, &expected, 1u, 0, __ATOMIC_ACQ_REL,
            __ATOMIC_RELAXED)) {
        (void)ring_push(PM_METAL_RING_KIND_TASK, task);
        return;
    }
    atomic_fetch_add(&s_busy, 1u);
    cur = current_slot();
    *cur = task;
    for (;;) {
        pm_metal_async_coro_t *leaf = task->root;
        int vm_held = 0;
        while (leaf->awaiting != NULL) {
            leaf = leaf->awaiting;
        }
        if (leaf->step == NULL) {
            leaf->status = PM_METAL_ASYNC_ERROR;
            break;
        }
        if (leaf->vm_only) {
            uint32_t slot;
            pm_metal_async_status_t mst;
            /* Only VM-capable runners may enter the interpreter. Before the
             * VM lock is up, serialization is moot and only the boot thread
             * qualifies. */
            if (!s_vm_lock_ready) {
                slot = cpu_slot();
                if (s_worker[slot] != 0u) {
                    *cur = NULL;
                    __atomic_store_n(&task->running, 0u, __ATOMIC_RELEASE);
                    atomic_fetch_sub(&s_busy, 1u);
                    (void)ring_push(PM_METAL_RING_KIND_TASK, task);
                    return;
                }
            } else {
                slot = cpu_slot();
                if (s_worker[slot] != 0u && !s_vm_capable[slot]) {
                    *cur = NULL;
                    __atomic_store_n(&task->running, 0u, __ATOMIC_RELEASE);
                    atomic_fetch_sub(&s_busy, 1u);
                    (void)ring_push(PM_METAL_RING_KIND_TASK, task);
                    return;
                }
                /* Try to claim the interpreter via the async mutex. On
                 * contention the task parks on the mutex FIFO and we hand it
                 * back — another core will wake it when it releases. */
                mst = pm_metal_async_mutex_try_acquire(&s_vm_mutex, leaf);
                if (mst == PM_METAL_ASYNC_WAITING) {
                    *cur = NULL;
                    __atomic_store_n(&task->running, 0u, __ATOMIC_RELEASE);
                    atomic_fetch_sub(&s_busy, 1u);
                    return;
                }
            }
            vm_held = 1;
        }
        pm_metal_async_status_t st = leaf->step(leaf);
        if (vm_held) {
            if (s_vm_lock_ready) {
                pm_metal_async_mutex_release(&s_vm_mutex);
            }
        }
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
    *cur = NULL;
    __atomic_store_n(&task->running, 0u, __ATOMIC_RELEASE);
    atomic_fetch_sub(&s_busy, 1u);
}

static void drain_one(void) {
    uint64_t word;
    uint32_t idx;
    if (ring_claim(&word, &idx) != 0) {
        return;
    }
    if (ring_kind(word) == PM_METAL_RING_KIND_STOP) {
        ring_release(idx);
        return;
    }
    pm_metal_async_task_t *task = (pm_metal_async_task_t *)pay_ptr(ring_pay(word));
    ring_release(idx);
    if (task == *current_slot()) {
        (void)ring_push(PM_METAL_RING_KIND_TASK, task);
        return;
    }
    step_task(task);
}

static void runner_entry(void *arg) {
#if defined(PM_METAL_ASYNC_PTHREAD)
    s_cpu = (uint32_t)(uintptr_t)arg;
#else
    (void)arg;
#endif
    /* A runner (AP or pthread) marks its slot a worker so cpu id / slot bookkeeping
     * is stable; vm_only stepping is no longer restricted to the boot thread.
     * Runners that re-enter the bytecode VM do so under the VM lock, and only if
     * the seat installed MicroPython thread state for them (s_vm_capable). */
    s_worker[cpu_slot()] = 1u;
    s_vm_capable[cpu_slot()] = (uint32_t)pm_metal_async_runner_begin(cpu_slot());
    atomic_fetch_add(&s_alive, 1u);
    while (atomic_load(&s_run) != 0u) {
        pm_metal_net_ip_pump();
        fire_timers();
        drain_one();
        if (ring_empty()) {
            idle_wait_us(200ull);
        }
    }
}

#if defined(PM_METAL_ASYNC_PTHREAD)
static void *pthread_entry(void *arg) {
    runner_entry(arg);
    return NULL;
}
#endif

int32_t pm_metal_async_init(pm_util_mem_arena_t *arena, uint32_t ncpu) {
    uint32_t want;
    if (arena == NULL || ncpu == 0u) {
        return -1;
    }
    if (s_ready) {
        return 0;
    }
    s_arena = arena;
    s_inbox = inbox_map(arena);
    if (s_inbox == NULL) {
        s_arena = NULL;
        return -1;
    }
    pm_util_lock_init(&s_timer_lock);
    pm_metal_async_mutex_init(&s_vm_mutex);
    s_vm_lock_ready = 1;
    s_timers = NULL;
    memset(s_worker, 0, sizeof(s_worker));
    memset(s_vm_capable, 0, sizeof(s_vm_capable));
    s_ncpu = 1;
    atomic_store(&s_alive, 1u);
    atomic_store(&s_run, 1u);
#if defined(PM_METAL_ASYNC_PTHREAD)
    s_cpu = 0;
    s_current = NULL;
    s_thread = NULL;
    s_njoin = 0;
#else
    memset(s_current_cpu, 0, sizeof(s_current_cpu));
    s_ncurrent = PM_METAL_ASYNC_APIC_N;
#endif
    s_ready = 1;
    want = ncpu;
    if (want > 1u) {
        int32_t st;
#if defined(PM_METAL_ASYNC_PTHREAD)
        {
            uint32_t i;
            s_thread = (pthread_t *)pm_util_mem_alloc(arena, (size_t)want * sizeof(*s_thread));
            if (s_thread == NULL) {
                st = -1;
            } else {
                memset(s_thread, 0, (size_t)want * sizeof(*s_thread));
                st = 0;
                for (i = 1; i < want; i++) {
                    if (pthread_create(&s_thread[i], NULL, pthread_entry, (void *)(uintptr_t)i) != 0) {
                        st = -1;
                        break;
                    }
                    s_njoin = i;
                }
            }
        }
#else
        st = pm_metal_async_fill_start_aps(arena, want, runner_entry);
#endif
        if (st == 0) {
            uint64_t t0 = pm_metal_async_mono_us();
            while (atomic_load(&s_alive) < want && pm_metal_async_mono_us() - t0 < 2000000ull) {
                idle_wait_us(200ull);
            }
            s_ncpu = atomic_load(&s_alive);
            if (s_ncpu == 0u) {
                s_ncpu = 1u;
            }
        }
    }
    return 0;
}

int32_t pm_metal_async_ready(void) {
    return s_ready ? 1 : 0;
}

uint32_t pm_metal_async_n_runners(void) {
    return s_ready ? s_ncpu : 0u;
}

const char *pm_metal_async_runner_kind(void) {
#if defined(PM_METAL_ASYNC_PTHREAD)
    return "pthread";
#elif defined(__EMSCRIPTEN__)
    return "sim asyncify";
#elif defined(PM_METAL_FIRMWARE)
    return "smp";
#else
    return "ok";
#endif
}

/* The arena the card booted with (async_init's argument). Cards whose
 * faces take no arena of their own (jit.c's compile coro, for example)
 * route in-kernel scratch allocations through it. */
pm_util_mem_arena_t *pm_metal_async_arena(void) {
    return s_ready ? s_arena : NULL;
}

void pm_metal_async_deinit(void) {
    uint32_t i;
    atomic_store(&s_run, 0u);
    if (s_inbox != NULL) {
        for (i = 1; i < s_ncpu; i++) {
            (void)ring_push(PM_METAL_RING_KIND_STOP, NULL);
        }
    }
#if defined(PM_METAL_ASYNC_PTHREAD)
    if (s_thread != NULL) {
        for (i = 1; i <= s_njoin; i++) {
            pthread_join(s_thread[i], NULL);
            s_thread[i] = 0;
        }
    }
    s_njoin = 0;
    s_thread = NULL;
#endif
    while (s_timers != NULL) {
        struct pm_metal_async_timer *tm = s_timers;
        s_timers = tm->next;
        if (s_arena != NULL) {
            pm_util_mem_free(s_arena, tm);
        }
    }
    s_inbox = NULL;
    s_arena = NULL;
#if defined(PM_METAL_ASYNC_PTHREAD)
    s_current = NULL;
#else
    memset(s_current_cpu, 0, sizeof(s_current_cpu));
#endif
    s_ncpu = 0;
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
    for (;;) {
        uint64_t word;
        uint32_t idx;
        if (ring_claim(&word, &idx) != 0) {
            break;
        }
        if (ring_kind(word) == PM_METAL_RING_KIND_STOP) {
            ring_release(idx);
            continue;
        }
        pm_metal_async_task_t *task = (pm_metal_async_task_t *)pay_ptr(ring_pay(word));
        ring_release(idx);
        step_task(task);
        pm_metal_net_ip_pump();
        fire_timers();
    }
}

int32_t pm_metal_async_run_until(pm_metal_async_coro_t *waiter) {
    if (!s_ready || waiter == NULL) {
        return -1;
    }
    pm_metal_async_task_t *owner = *current_slot();
    if (owner != NULL) {
        owner->on_c_stack = 1;
    }
    /* An empty ready ring does not mean stuck: a wakeup pushed by a worker, or
     * a packet the next pump will deliver, is still on its way. Give up only
     * when nothing has moved for a whole stall window, or a loaded box turns a
     * live wait into a failure. */
    uint64_t stall_until = pm_metal_async_mono_us() + PM_METAL_ASYNC_STALL_US;
    while (waiter->status != PM_METAL_ASYNC_DONE && waiter->status != PM_METAL_ASYNC_ERROR
        && waiter->status != PM_METAL_ASYNC_CANCELLED) {
        pm_metal_net_ip_pump();
        fire_timers();
        uint64_t word;
        uint32_t idx;
        if (ring_claim(&word, &idx) == 0) {
            if (ring_kind(word) == PM_METAL_RING_KIND_STOP) {
                ring_release(idx);
                continue;
            }
            pm_metal_async_task_t *task = (pm_metal_async_task_t *)pay_ptr(ring_pay(word));
            ring_release(idx);
            stall_until = pm_metal_async_mono_us() + PM_METAL_ASYNC_STALL_US;
            if (task == owner) {
                continue;
            }
            step_task(task);
            continue;
        }
        uint64_t next = next_timer_deadline();
        if (next == UINT64_MAX) {
            if (atomic_load(&s_busy) != 0u) {
                stall_until = pm_metal_async_mono_us() + PM_METAL_ASYNC_STALL_US;
                idle_wait_us(50ull);
                continue;
            }
            if (pm_metal_async_mono_us() >= stall_until) {
                if (owner != NULL) {
                    owner->on_c_stack = 0;
                }
                return -1;
            }
            idle_wait_us(50ull);
            continue;
        }
        uint64_t now = pm_metal_async_mono_us();
        uint64_t wait = next > now ? next - now : 0;
        if (wait > 1000000ull) {
            wait = 1000000ull;
        }
        idle_wait_us(wait);
        stall_until = pm_metal_async_mono_us() + PM_METAL_ASYNC_STALL_US;
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
    return *current_slot();
}

uint32_t pm_metal_async_process_id(void) {
    pm_metal_async_task_t *t = pm_metal_async_current_task();
    if (t == NULL) {
        return 0;
    }
    return t->pid;
}

int32_t pm_metal_async_post_task(pm_metal_async_task_t *task) {
    if (!s_ready || task == NULL) {
        return -1;
    }
    return ring_push(PM_METAL_RING_KIND_TASK, task);
}

static int32_t pm_metal_async_boot(pm_util_mem_arena_t *arena) {
    uint32_t n = pm_metal_async_fill_ncpu();
    if (n == 0u) {
        n = 1u;
    }
    return pm_metal_async_init(arena, n);
}

/* Strong fill for wasmmod's freestanding io yield hook
 * (ports/freestanding/io_ops.h): a parked fetch checkpoints our runner. */
uint32_t pm_wasmmod_host_io_yield(void) {
    return pm_metal_async_yield();
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_init, pm_metal_async_init, int32_t(pm_util_mem_arena_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_deinit, pm_metal_async_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_arena, pm_metal_async_arena, pm_util_mem_arena_t *(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_mutex_init, pm_metal_async_mutex_init, void(pm_metal_async_mutex_t *));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_mutex_try_acquire, pm_metal_async_mutex_try_acquire, pm_metal_async_status_t(pm_metal_async_mutex_t *, pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_mutex_release, pm_metal_async_mutex_release, void(pm_metal_async_mutex_t *));
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
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_process_id, pm_metal_async_process_id, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_post_task, pm_metal_async_post_task, int32_t(pm_metal_async_task_t *));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_ready, pm_metal_async_ready, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.async, pm_metal_async_n_runners, pm_metal_async_n_runners, uint32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.async, pm_metal_async_boot, pm_metal_async_deinit);
