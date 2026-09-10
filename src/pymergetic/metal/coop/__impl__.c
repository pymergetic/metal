/* pymergetic.metal.coop — stackless runner (new; not a port of run.c/coro.c).
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
#include "pymergetic/metal/coop/__exports__.h"

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
#define PM_METAL_COOP_HAVE_POLL 1
#endif
#if !defined(__EMSCRIPTEN__)
#include <pthread.h>
#define PM_METAL_COOP_PTHREAD 1
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
#define PM_METAL_COOP_RING_WANT (1u << 20)
#define PM_METAL_COOP_RING_MIN 256u
/* xAPIC CPUID leaf 1 id is 8-bit. Lookup width, not a core cap. */
#define PM_METAL_COOP_APIC_N 256u
/* How long run_until keeps waiting with nothing runnable before it calls the
 * wait dead. Must survive one guest-TCP retransmit cycle: a dropped packet
 * on a real wire (QEMU user-net under load drops) waits out the initial RTO
 * (~200-300ms) with nothing runnable, and firing first turns a healthy
 * fetch into "fetch failed". 1s tolerates a retransmit and still catches a
 * genuinely dead wait far below any human timeout. */
#define PM_METAL_COOP_STALL_US 1000000ull

struct pm_metal_coop_timer {
    struct pm_metal_coop_timer *next;
    uint64_t deadline_us;
    pm_metal_coop_task_t *task;
};

typedef struct {
    uint32_t n;
    uint32_t mask;
    atomic_uint head;
    atomic_uint tail;
    atomic_uint count;
    _Alignas(8) atomic_uint_least64_t slot[];
} pm_metal_coop_inbox_t;

static pm_util_mem_arena_t *s_arena;
static pm_metal_coop_inbox_t *s_inbox;
static struct pm_metal_coop_timer *s_timers;
static pm_util_lock_t s_timer_lock;
/* VM-entry mutex: async-aware (park on contention, wake on release, never spin).
 * The interpreter is one resource shared by every runner core; a vm_only coro
 * takes this mutex for the duration of its step. Replaces the ad-hoc
 * vm_enter/vm_leave hand-back on raw s_vm_lock. */
static pm_metal_coop_mutex_t s_vm_mutex;
static uint32_t s_vm_lock_ready;
static uint32_t s_ready;
static uint32_t s_ncpu;
/* Worker (AP / runner pthread) marking per current-slot. Kept for CPU id / slot
 * bookkeeping; it no longer gates vm_only stepping. A plain slot array keeps
 * TLS out of firmware. */
static uint32_t s_worker[PM_METAL_COOP_APIC_N];
/* Per-slot: 1 if that runner installed MicroPython thread state and may re-enter
 * the bytecode VM. The boot thread (s_worker == 0) is always VM-capable. */
static uint32_t s_vm_capable[PM_METAL_COOP_APIC_N];
#if defined(PM_METAL_COOP_PTHREAD)
static uint32_t s_njoin;
static pthread_t *s_thread;
static __thread uint32_t s_cpu;
static __thread pm_metal_coop_task_t *s_current;
#else
static pm_metal_coop_task_t *s_current_cpu[PM_METAL_COOP_APIC_N];
static uint32_t s_ncurrent = PM_METAL_COOP_APIC_N;
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

__attribute__((weak)) uint32_t pm_metal_coop_fill_ncpu(void) {
#if defined(PM_METAL_COOP_PTHREAD)
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
__attribute__((weak)) int pm_metal_coop_runner_begin(uint32_t slot) {
    (void)slot;
    return 0;
}

#if !defined(PM_METAL_COOP_PTHREAD)
__attribute__((weak)) int32_t pm_metal_coop_fill_start_aps(pm_util_mem_arena_t *arena, uint32_t ncpu,
    void (*entry)(void *)) {
    (void)arena;
    (void)ncpu;
    (void)entry;
    return -1;
}
#endif

#if !defined(PM_METAL_COOP_PTHREAD)
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
#elif defined(PM_METAL_FIRMWARE) && defined(__arm__) && !defined(__aarch64__)
    uint32_t mpidr;
    /* MPIDR Aff0 — the AP PSCI CPU_ON targeted (cpu index within the
     * cluster; QEMU virt and RV1106 are single-cluster). */
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
    mpidr &= 0xffu;
    if (mpidr >= s_ncurrent) {
        return 0;
    }
    return mpidr;
#else
    return 0;
#endif
}
#endif

static pm_metal_coop_task_t **current_slot(void) {
#if defined(PM_METAL_COOP_PTHREAD)
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
#if defined(PM_METAL_COOP_PTHREAD)
    return s_cpu;
#else
    uint32_t id = cpu_id();
    if (id >= PM_METAL_COOP_APIC_N) {
        id = 0;
    }
    return id;
#endif
}

uint64_t pm_metal_coop_mono_us(void) {
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
    t0 = pm_metal_coop_mono_us();
    while (pm_metal_coop_mono_us() - t0 < us) {
        pm_cpu_pause();
    }
#else
    int32_t fd;
#if defined(PM_METAL_COOP_HAVE_POLL)
    struct pollfd pfd;
    int timeout_ms;
#endif
    if (us == 0) {
        return;
    }
    fd = pm_metal_drivers_net_tap_fd();
#if defined(PM_METAL_COOP_HAVE_POLL)
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
    return sizeof(pm_metal_coop_inbox_t) + (size_t)n * sizeof(atomic_uint_least64_t);
}

static pm_metal_coop_inbox_t *inbox_map(pm_util_mem_arena_t *arena) {
    uint32_t n = PM_METAL_COOP_RING_WANT;
    while (n >= PM_METAL_COOP_RING_MIN) {
        size_t bytes = inbox_bytes(n);
        pm_metal_coop_inbox_t *in = (pm_metal_coop_inbox_t *)pm_util_mem_alloc(arena, bytes);
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
    pm_metal_coop_inbox_t *in = s_inbox;
    uint64_t neu = ring_pack(PM_METAL_RING_ST_READY, kind, ptr_pay(payload));
    uint32_t n;
    uint32_t mask;
    uint32_t start;
    uint32_t hunt;
    uint32_t i;
    uint32_t attempt;
    if (in == NULL) {
        return -1;
    }
    n = in->n;
    mask = in->mask;
    /* Under concurrent pushers the tail snapshot can go stale mid-hunt
     * (another core CAS'd tail forward), so the window computed from it can
     * miss the free region and refuse spuriously — with a fan-out of
     * submitters (walk + its job tasks) that misrefusal once killed a
     * supervisor's self re-post and with it the whole walk. Retry the
     * snapshot a few times: a genuinely full ring still refuses, a raced
     * one resolves on the next attempt. */
    for (attempt = 0; attempt < 4u; attempt++) {
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
        if (atomic_load(&in->count) >= n) {
            return -1;  /* genuinely full */
        }
    }
    return -1;
}

static int32_t ring_claim(uint64_t *word_out, uint32_t *idx_out) {
    pm_metal_coop_inbox_t *in = s_inbox;
    uint32_t n;
    uint32_t mask;
    uint32_t start;
    uint32_t i;
    if (in == NULL || atomic_load(&in->count) == 0u) {
        return -1;
    }
    n = in->n;
    mask = in->mask;
    start = atomic_load(&in->head);
    /* The window is the WHOLE ring, not count+16 from head: a push can
     * fill a slot BEHIND head (claim's head jump skips the stretch
     * between the old head and its claimed slot, and a concurrent push
     * may fill one of those skipped slots right after the scan passed
     * it). A count+16 window reaches that slot only after wrapping
     * nearly the whole ring — when it is the last ref left, nothing
     * else advances head and the window never reaches it: count>0,
     * no READY slot in window, every runner spins claim-empty forever
     * (the BUILD-ALL livelock: the walk's park ref and a NEW job's
     * post ref both sat behind head; the census froze at 31 done / 4
     * lanes). A full scan of n slots (256 on this seat's arena) makes
     * every filled slot reachable by construction — count>0 with no
     * claimable slot then genuinely means all counted slots are
     * CLAIMED-in-flight, and idle_wait is the right answer. */
    for (i = 0; i < n; i++) {
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
    pm_metal_coop_inbox_t *in = s_inbox;
    if (in == NULL || idx >= in->n) {
        return;
    }
    atomic_store(&in->slot[idx], 0);
    atomic_fetch_sub(&in->count, 1u);
}

static pm_metal_coop_task_t *task_of(pm_metal_coop_coro_t *c) {
    while (c != NULL) {
        if (c->task != NULL) {
            return c->task;
        }
        c = c->waiter;
    }
    return NULL;
}

/* ===== Task reclamation (refcount, not epoch) =====
 *
 * A terminal task's blocks (task + optional coro frame) are freed when
 * the LAST reference drops. References: every ready-ring slot holding
 * the task (push +1; the driver that claims the slot holds that ref
 * until it is done with the task), plus the dead-marker's transient ref
 * around the dead transition. The ONLY free path is a decrement to zero
 * with dead set — one place, no double free. Pushing a dead task is
 * refused (and a push that raced the dead bit re-checks after its
 * increment and unwinds), so the count cannot grow once dead is set.
 * This closes the push/drain TOCTOU an epoch list has (a task pushed
 * just as the ring emptied would dangle). */
static int32_t task_is_terminal(pm_metal_coop_task_t *t) {
    uint32_t st;
    if (t == NULL || t->root == NULL) {
        return 1;
    }
    st = t->root->status;
    return st == PM_METAL_COOP_DONE || st == PM_METAL_COOP_CANCELLED
        || st == PM_METAL_COOP_ERROR;
}

/* The one free path: last decrement with dead set. May free `task` (and
 * the coro frame when the coro is auto_free — a coro_create block, coro
 * first member); callers must not touch the task afterwards. */
static void task_unref(pm_metal_coop_task_t *task) {
    pm_util_mem_arena_t *arena;
    pm_metal_coop_coro_t *root;
    pm_metal_coop_coro_t *frame;
    if (task == NULL) {
        return;
    }
    if (__atomic_fetch_sub(&task->ring_refs, 1u, __ATOMIC_ACQ_REL) != 1u) {
        return;
    }
    if (__atomic_load_n(&task->dead, __ATOMIC_ACQUIRE) == 0u) {
        return;
    }
    arena = s_arena;
    root = task->root;
    frame = (root != NULL && root->auto_free != 0u) ? root : NULL;
    pm_util_mem_free(arena, task);
    if (frame != NULL) {
        pm_util_mem_free(arena, frame);
    }
}

/* Push one task ref. Returns -1 (and does not push) when the task is
 * dead — a dead task's blocks are being freed; a ring slot to it would
 * be a dangling payload. The dead re-check after the increment unwinds
 * a push that raced the dead bit. */
static int32_t push_task_ref(pm_metal_coop_task_t *task) {
    if (task == NULL) {
        return -1;
    }
    if (__atomic_load_n(&task->dead, __ATOMIC_ACQUIRE) != 0u) {
        return -1;
    }
    __atomic_fetch_add(&task->ring_refs, 1u, __ATOMIC_ACQ_REL);
    if (__atomic_load_n(&task->dead, __ATOMIC_ACQUIRE) != 0u) {
        task_unref(task);
        return -1;
    }
    if (ring_push(PM_METAL_RING_KIND_TASK, task) != 0) {
        task_unref(task);
        return -1;
    }
    return 0;
}

/* Mark a terminal task dead: its blocks go home on the last unref. The
 * transient ref around the exchange closes the in-flight-push race (the
 * push's own unwind then sees the bit and takes the last drop itself). */
static void task_mark_dead(pm_metal_coop_task_t *task) {
    uint32_t one = 1u;
    uint32_t old = 0u;
    if (task == NULL) {
        return;
    }
    __atomic_fetch_add(&task->ring_refs, 1u, __ATOMIC_ACQ_REL);
    /* __atomic_exchange (ptr, &desired, &ret, order), not the _n spelling:
     * the _n forms have no fallback in the vendored TCC's stdatomic.h
     * (upstream 36ff4f5 added load_n/store_n/compare_exchange_n but skipped
     * exchange_n), so TCC parses it as a plain call and the ksweep link
     * refuses with an unresolved symbol. The plain form is the identical
     * lock xchg; GCC/Clang lower it the same way, and old receives the
     * previous dead bit. */
    __atomic_exchange(&task->dead, &one, &old, __ATOMIC_ACQ_REL);
    if (old != 0u) {
        task_unref(task);
        return;
    }
    task_unref(task);
}

int32_t pm_metal_coop_task_reclaim(pm_metal_coop_task_t *task) {
    if (task == NULL) {
        return -1;
    }
    if (!task_is_terminal(task)) {
        return -1;
    }
    task_mark_dead(task);
    return 0;
}

int32_t pm_metal_coop_task_detach(pm_metal_coop_task_t *task) {
    if (task == NULL) {
        return -1;
    }
    if (!task_is_terminal(task)) {
        return -1;
    }
    /* clear the root before marking dead: the last ref's unref reads
     * root->auto_free to decide whether to free the frame too — a NULL
     * root means "frame is the owner's business" and skips that read.
     * The store is atomic because a concurrent unref on another core may
     * read root the moment its own ref drops. */
    __atomic_store_n(&task->root, NULL, __ATOMIC_RELEASE);
    task_mark_dead(task);
    return 0;
}

static void timer_insert(struct pm_metal_coop_timer *tm) {
    struct pm_metal_coop_timer **pp;
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
    uint64_t now = pm_metal_coop_mono_us();
    pm_util_lock_acquire(&s_timer_lock);
    while (s_timers != NULL && s_timers->deadline_us <= now) {
        struct pm_metal_coop_timer *tm = s_timers;
        s_timers = tm->next;
        pm_util_lock_release(&s_timer_lock);
        (void)push_task_ref(tm->task);
        /* drop the timer's own ref (taken by sleep_us): the task outlives
         * its timer even when it finished and was reclaimed between arming
         * and firing — the stale-timer push above is refused on dead, and
         * this unref is what retires a task that was parked when it ended. */
        task_unref(tm->task);
        pm_util_mem_free(s_arena, tm);
        pm_util_lock_acquire(&s_timer_lock);
        now = pm_metal_coop_mono_us();
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

void pm_metal_coop_coro_set_vm_only(pm_metal_coop_coro_t *coro) {
    if (coro != NULL) {
        coro->vm_only = 1u;
    }
}

void pm_metal_coop_coro_set_auto_free(pm_metal_coop_coro_t *coro) {
    if (coro != NULL) {
        coro->auto_free = 1u;
    }
}

/* ===== Async mutex: one reusable cast (park-on-contention, wake-on-release). ===== */

void pm_metal_coop_mutex_init(pm_metal_coop_mutex_t *m) {
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof(*m));
    pm_util_lock_init(&m->fifo_lock);
}

/* Try to claim the mutex. On success (owner: NULL→task CAS passes) returns
 * PM_METAL_COOP_PENDING — the caller must then step the coro. On contention
 * the current task is parked on the FIFO and PM_METAL_COOP_WAITING is
 * returned; the runner hands the task back to the ready ring.
 *
 * Race window between CAS fail and push-to-FIFO: the holder may release and
 * pop the FIFO between our fail and our push. The fifo_lock serializes that
 * window — under the lock, re-check owner after the push; if owner is NULL
 * now (release popped nothing), self-claim and return PENDING. */
pm_metal_coop_status_t pm_metal_coop_mutex_try_acquire(pm_metal_coop_mutex_t *m, pm_metal_coop_coro_t *self) {
    pm_metal_coop_task_t *task;
    pm_metal_coop_task_t *expected = NULL;
    if (m == NULL || self == NULL) {
        return PM_METAL_COOP_ERROR;
    }
    task = task_of(self);
    if (task == NULL) {
        return PM_METAL_COOP_ERROR;
    }
    /* Re-entry after a wake: the mutex release transferred ownership to this
     * task already. Proceed without any CAS. */
    if (m->owner == task) {
        return PM_METAL_COOP_PENDING;
    }
    if (__atomic_compare_exchange_n(&m->owner, &expected, task, 0, __ATOMIC_ACQ_REL,
            __ATOMIC_RELAXED)) {
        return PM_METAL_COOP_PENDING;
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
        pm_metal_coop_task_t *prev = NULL;
        pm_metal_coop_task_t *cur = m->waiters_head;
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
        return PM_METAL_COOP_PENDING;
    }
    pm_util_lock_release(&m->fifo_lock);
    return PM_METAL_COOP_WAITING;
}

/* Release the mutex. If a task is waiting, transfer ownership directly (the
 * woken task becomes m->owner) and push it to the ready ring. Under
 * fifo_lock the pop is atomic with respect to the contending try_acquire
 * re-check, so no task is lost. */
void pm_metal_coop_mutex_release(pm_metal_coop_mutex_t *m) {
    pm_metal_coop_task_t *wake;
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
        (void)push_task_ref(wake);
    }
}

static void step_task(pm_metal_coop_task_t *task) {
    pm_metal_coop_task_t **cur;
    if (task == NULL || task->root == NULL || task->on_c_stack) {
        return;
    }
    if (task->root->status == PM_METAL_COOP_DONE || task->root->status == PM_METAL_COOP_ERROR
        || task->root->status == PM_METAL_COOP_CANCELLED) {
        /* terminal on arrival (a stale ring slot): the pusher's ref goes
         * home; when dead, this is also the reclaimer's last drop. */
        task_unref(task);
        return;
    }
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&task->running, &expected, 1u, 0, __ATOMIC_ACQ_REL,
            __ATOMIC_RELAXED)) {
        (void)push_task_ref(task);
        return;
    }
    atomic_fetch_add(&s_busy, 1u);
    cur = current_slot();
    *cur = task;
    for (;;) {
        pm_metal_coop_coro_t *leaf = task->root;
        int vm_held = 0;
        while (leaf->awaiting != NULL) {
            leaf = leaf->awaiting;
        }
        if (leaf->step == NULL) {
            leaf->status = PM_METAL_COOP_ERROR;
            break;
        }
        if (leaf->vm_only) {
            uint32_t slot;
            pm_metal_coop_status_t mst;
            /* Only VM-capable runners may enter the interpreter. Before the
             * VM lock is up, serialization is moot and only the boot thread
             * qualifies. */
            if (!s_vm_lock_ready) {
                slot = cpu_slot();
                if (s_worker[slot] != 0u) {
                    *cur = NULL;
                    __atomic_store_n(&task->running, 0u, __ATOMIC_RELEASE);
                    atomic_fetch_sub(&s_busy, 1u);
                    (void)push_task_ref(task);
                    return;
                }
            } else {
                slot = cpu_slot();
                if (s_worker[slot] != 0u && !s_vm_capable[slot]) {
                    *cur = NULL;
                    __atomic_store_n(&task->running, 0u, __ATOMIC_RELEASE);
                    atomic_fetch_sub(&s_busy, 1u);
                    (void)push_task_ref(task);
                    return;
                }
                /* Try to claim the interpreter via the async mutex. On
                 * contention the task parks on the mutex FIFO and we hand it
                 * back — another core will wake it when it releases. */
                mst = pm_metal_coop_mutex_try_acquire(&s_vm_mutex, leaf);
                if (mst == PM_METAL_COOP_WAITING) {
                    *cur = NULL;
                    __atomic_store_n(&task->running, 0u, __ATOMIC_RELEASE);
                    atomic_fetch_sub(&s_busy, 1u);
                    return;
                }
            }
            vm_held = 1;
        }
        pm_metal_coop_status_t st = leaf->step(leaf);
        if (vm_held) {
            if (s_vm_lock_ready) {
                pm_metal_coop_mutex_release(&s_vm_mutex);
            }
        }
        leaf->status = (uint32_t)st;
        if (st == PM_METAL_COOP_WAITING) {
            if (leaf->awaiting != NULL) {
                continue;
            }
            break;
        }
        if (st == PM_METAL_COOP_DONE && leaf->waiter != NULL) {
            leaf->waiter->awaiting = NULL;
            continue;
        }
        break;
    }
    *cur = NULL;
    __atomic_store_n(&task->running, 0u, __ATOMIC_RELEASE);
    atomic_fetch_sub(&s_busy, 1u);
    /* The step that produced the terminal state retires the task: an
     * auto_free coro's blocks are dead from here on (the caller's ref,
     * dropped after this returns, is the last one). A detached root
     * (owner already moved on) skips the frame-free retirement — the
     * owner's own reclaim/detach owns that task block. */
    if (task_is_terminal(task) && task->root != NULL
        && task->root->auto_free != 0u) {
        task_mark_dead(task);
    }
}

/* 0 = claimed and ran (or parked) something, -1 = nothing claimable
 * (empty ring, or every counted slot is CLAIMED-in-flight). Callers
 * driving a loop use the signal to pick their idle nap length. */
static int32_t drain_one(void) {
    uint64_t word;
    uint32_t idx;
    if (ring_claim(&word, &idx) != 0) {
        return -1;
    }
    if (ring_kind(word) == PM_METAL_RING_KIND_STOP) {
        ring_release(idx);
        return 0;
    }
    pm_metal_coop_task_t *task = (pm_metal_coop_task_t *)pay_ptr(ring_pay(word));
    ring_release(idx);
    if (task == *current_slot()) {
        /* our own task (a nested run_until on this core owns its C
         * frame): park it back, never step it here. The claimed ref
         * (the pusher's, ours since the claim) is dropped — the
         * re-push below takes its own fresh ref, and the two must
         * not stack (an orphaned claimed ref inflates ring_refs past
         * the slot count and pins the task block forever). */
        if (push_task_ref(task) == 0) {
            task_unref(task);
        } else {
            /* ring full: the claimed ref is the task's last live
             * link — dropping it would strand the task. Step it
             * nowhere; leave the ref and let the walk's own run_until
             * drive it (on_c_stack pins it off this path anyway). */
        }
        return 0;
    }
    step_task(task);
    /* the slot's ref (taken by the pusher) is ours to drop now */
    task_unref(task);
    return 0;
}

static void runner_entry(void *arg) {
#if defined(PM_METAL_COOP_PTHREAD)
    s_cpu = (uint32_t)(uintptr_t)arg;
#else
    (void)arg;
#endif
    /* A runner (AP or pthread) marks its slot a worker so cpu id / slot bookkeeping
     * is stable; vm_only stepping is no longer restricted to the boot thread.
     * Runners that re-enter the bytecode VM do so under the VM lock, and only if
     * the seat installed MicroPython thread state for them (s_vm_capable). */
    s_worker[cpu_slot()] = 1u;
    s_vm_capable[cpu_slot()] = (uint32_t)pm_metal_coop_runner_begin(cpu_slot());
    atomic_fetch_add(&s_alive, 1u);
    /* Idle backoff ladder: after a claim ran work the runner naps at the
     * floor (200us) for pickup latency; every consecutive nothing-claimable
     * pass doubles the nap, up to a ceiling, and any claim resets it. The
     * ceiling is clamped by the next timer deadline so a sleeping task
     * never waits longer than its own deadline once the runner parks.
     * Without this, an idle seat burns a full core per runner: 200us
     * poll(2) + full ip pump + timer scan per pass is ~470us of work per
     * ~200us nap, measured ~70% CPU per runner on a parked serve() REPL.
     * Firmware (rdtsc pause) gets the same ladder; there the nap is a
     * pause spin, so the ladder reads as progressive pause length. */
    uint32_t backoff = 0u;
    while (atomic_load(&s_run) != 0u) {
        int32_t claimed;
        pm_metal_net_ip_pump();
        fire_timers();
        claimed = drain_one();
        if (claimed == 0) {
            backoff = 0u;
        } else {
            uint64_t nap = 200ull << backoff;
            uint64_t next;
            if (backoff < 6u) {
                backoff++;
            }
            next = next_timer_deadline();
            if (next != UINT64_MAX) {
                uint64_t now = pm_metal_coop_mono_us();
                uint64_t span = next > now ? next - now : 0ull;
                if (span < nap) {
                    nap = span;
                }
            }
            idle_wait_us(nap);
        }
    }
}

#if defined(PM_METAL_COOP_PTHREAD)
static void *pthread_entry(void *arg) {
    runner_entry(arg);
    return NULL;
}
#endif

int32_t pm_metal_coop_init(pm_util_mem_arena_t *arena, uint32_t ncpu) {
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
    pm_metal_coop_mutex_init(&s_vm_mutex);
    s_vm_lock_ready = 1;
    s_timers = NULL;
    memset(s_worker, 0, sizeof(s_worker));
    memset(s_vm_capable, 0, sizeof(s_vm_capable));
    s_ncpu = 1;
    atomic_store(&s_alive, 1u);
    atomic_store(&s_run, 1u);
#if defined(PM_METAL_COOP_PTHREAD)
    s_cpu = 0;
    s_current = NULL;
    s_thread = NULL;
    s_njoin = 0;
#else
    memset(s_current_cpu, 0, sizeof(s_current_cpu));
    s_ncurrent = PM_METAL_COOP_APIC_N;
#endif
    s_ready = 1;
    want = ncpu;
    if (want > 1u) {
        int32_t st;
#if defined(PM_METAL_COOP_PTHREAD)
        {
            uint32_t i;
            s_thread = (pthread_t *)pm_util_mem_alloc(arena, (size_t)want * sizeof(*s_thread));
            if (s_thread == NULL) {
                st = -1;
            } else {
                pthread_attr_t attr;
                memset(s_thread, 0, (size_t)want * sizeof(*s_thread));
                st = 0;
                /* Runner stacks must clear TCC's recursive parser with the
                 * biggest card (zenoh's include tree eats several MiB of C
                 * recursion) ON TOP of whatever nested work the task already
                 * did — the build walk compiles units on runners now. The
                 * default 8 MiB is marginal; 32 MiB virtual is cheap (lazy
                 * pages) and headroom, not a raise of resident cost. */
                if (pthread_attr_init(&attr) != 0
                    || pthread_attr_setstacksize(&attr, 32u * 1024u * 1024u) != 0) {
                    st = -1;
                }
                for (i = 1; st == 0 && i < want; i++) {
                    if (pthread_create(&s_thread[i], &attr, pthread_entry, (void *)(uintptr_t)i) != 0) {
                        st = -1;
                        break;
                    }
                    s_njoin = i;
                }
                pthread_attr_destroy(&attr);
            }
        }
#else
        st = pm_metal_coop_fill_start_aps(arena, want, runner_entry);
#endif
        if (st == 0) {
            uint64_t t0 = pm_metal_coop_mono_us();
            while (atomic_load(&s_alive) < want && pm_metal_coop_mono_us() - t0 < 2000000ull) {
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

int32_t pm_metal_coop_ready(void) {
    return s_ready ? 1 : 0;
}

uint32_t pm_metal_coop_n_runners(void) {
    return s_ready ? s_ncpu : 0u;
}

const char *pm_metal_coop_runner_kind(void) {
#if defined(PM_METAL_COOP_PTHREAD)
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
pm_util_mem_arena_t *pm_metal_coop_arena(void) {
    return s_ready ? s_arena : NULL;
}

void pm_metal_coop_deinit(void) {
    uint32_t i;
    atomic_store(&s_run, 0u);
    if (s_inbox != NULL) {
        for (i = 1; i < s_ncpu; i++) {
            (void)ring_push(PM_METAL_RING_KIND_STOP, NULL);
        }
    }
#if defined(PM_METAL_COOP_PTHREAD)
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
        struct pm_metal_coop_timer *tm = s_timers;
        s_timers = tm->next;
        task_unref(tm->task); /* the timer's own ref (sleep_us) goes home */
        if (s_arena != NULL) {
            pm_util_mem_free(s_arena, tm);
        }
    }
    s_inbox = NULL;
    s_arena = NULL;
#if defined(PM_METAL_COOP_PTHREAD)
    s_current = NULL;
#else
    memset(s_current_cpu, 0, sizeof(s_current_cpu));
#endif
    s_ncpu = 0;
    s_ready = 0;
}

pm_metal_coop_coro_t *pm_metal_coop_coro_create(pm_metal_coop_step_fn step, size_t frame_bytes) {
    if (!s_ready || step == NULL || frame_bytes < sizeof(pm_metal_coop_coro_t)) {
        return NULL;
    }
    pm_metal_coop_coro_t *c = (pm_metal_coop_coro_t *)pm_util_mem_alloc(s_arena, frame_bytes);
    if (c == NULL) {
        return NULL;
    }
    memset(c, 0, frame_bytes);
    c->step = step;
    c->status = PM_METAL_COOP_PENDING;
    return c;
}

pm_metal_coop_task_t *pm_metal_coop_create_task(pm_metal_coop_coro_t *coro) {
    if (!s_ready || coro == NULL) {
        return NULL;
    }
    pm_metal_coop_task_t *t = (pm_metal_coop_task_t *)pm_util_mem_alloc(s_arena, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    memset(t, 0, sizeof(*t));
    t->root = coro;
    coro->task = t;
    /* the initial ref: the slot this pushes, dropped by the driver that
     * first claims it */
    if (push_task_ref(t) != 0) {
        coro->task = NULL;
        pm_util_mem_free(s_arena, t);
        return NULL;
    }
    return t;
}

pm_metal_coop_status_t pm_metal_coop_await(pm_metal_coop_coro_t *self, pm_metal_coop_coro_t *child) {
    if (self == NULL || child == NULL) {
        return PM_METAL_COOP_ERROR;
    }
    if (self->awaiting != NULL && self->awaiting != child) {
        return PM_METAL_COOP_ERROR;
    }
    if (child->status == PM_METAL_COOP_DONE) {
        self->awaiting = NULL;
        return PM_METAL_COOP_DONE;
    }
    self->awaiting = child;
    child->waiter = self;
    if (child->task == NULL) {
        child->task = task_of(self);
    }
    self->status = PM_METAL_COOP_WAITING;
    return PM_METAL_COOP_WAITING;
}

pm_metal_coop_status_t pm_metal_coop_yield_park(pm_metal_coop_coro_t *self) {
    pm_metal_coop_task_t *t = task_of(self);
    if (t == NULL) {
        return PM_METAL_COOP_ERROR;
    }
    self->status = PM_METAL_COOP_WAITING;
    /* the pusher ref is dropped by the driver that claims the slot
     * (drain_one/poll/run_until after step_task) */
    if (push_task_ref(t) != 0) {
        return PM_METAL_COOP_ERROR;
    }
    return PM_METAL_COOP_WAITING;
}

pm_metal_coop_status_t pm_metal_coop_sleep_us(pm_metal_coop_coro_t *self, uint64_t us) {
    pm_metal_coop_task_t *t = task_of(self);
    if (t == NULL || !s_ready) {
        return PM_METAL_COOP_ERROR;
    }
    struct pm_metal_coop_timer *tm =
        (struct pm_metal_coop_timer *)pm_util_mem_alloc(s_arena, sizeof(*tm));
    if (tm == NULL) {
        return PM_METAL_COOP_ERROR;
    }
    tm->next = NULL;
    tm->deadline_us = pm_metal_coop_mono_us() + us;
    tm->task = t;
    /* The timer owns a task ref for its lifetime: fire_timers pushes a fresh
     * ref (the ring slot's) and drops this one, and deinit drops every armed
     * timer's ref. Without it, a task that finished while parked (woken by
     * an external post_task rather than its timer) is reclaimed before its
     * timer fires — the stale timer then pushes freed memory. */
    __atomic_fetch_add(&t->ring_refs, 1u, __ATOMIC_ACQ_REL);
    timer_insert(tm);
    self->status = PM_METAL_COOP_WAITING;
    return PM_METAL_COOP_WAITING;
}

uint32_t pm_metal_coop_yield(void) {
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

void pm_metal_coop_poll(void) {
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
        pm_metal_coop_task_t *task = (pm_metal_coop_task_t *)pay_ptr(ring_pay(word));
        ring_release(idx);
        step_task(task);
        task_unref(task);
        pm_metal_net_ip_pump();
        fire_timers();
    }
}

int32_t pm_metal_coop_run_until(pm_metal_coop_coro_t *waiter) {
    if (!s_ready || waiter == NULL) {
        return -1;
    }
    pm_metal_coop_task_t *owner = *current_slot();
    if (owner != NULL) {
        owner->on_c_stack = 1;
    }
    /* An empty ready ring does not mean stuck: a wakeup pushed by a worker, or
     * a packet the next pump will deliver, is still on its way. Give up only
     * when nothing has moved for a whole stall window, or a loaded box turns a
     * live wait into a failure. */
    uint64_t stall_until = pm_metal_coop_mono_us() + PM_METAL_COOP_STALL_US;
    while (waiter->status != PM_METAL_COOP_DONE && waiter->status != PM_METAL_COOP_ERROR
        && waiter->status != PM_METAL_COOP_CANCELLED) {
        pm_metal_net_ip_pump();
        fire_timers();
        uint64_t word;
        uint32_t idx;
        if (ring_claim(&word, &idx) == 0) {
            if (ring_kind(word) == PM_METAL_RING_KIND_STOP) {
                ring_release(idx);
                continue;
            }
            pm_metal_coop_task_t *task = (pm_metal_coop_task_t *)pay_ptr(ring_pay(word));
            ring_release(idx);
            stall_until = pm_metal_coop_mono_us() + PM_METAL_COOP_STALL_US;
            if (task == owner) {
                task_unref(task);
                continue;
            }
            step_task(task);
            task_unref(task);
            continue;
        }
        uint64_t next = next_timer_deadline();
        if (next == UINT64_MAX) {
            if (atomic_load(&s_busy) != 0u) {
                stall_until = pm_metal_coop_mono_us() + PM_METAL_COOP_STALL_US;
                idle_wait_us(50ull);
                continue;
            }
            if (pm_metal_coop_mono_us() >= stall_until) {
                if (owner != NULL) {
                    owner->on_c_stack = 0;
                }
                return -1;
            }
            idle_wait_us(50ull);
            continue;
        }
        uint64_t now = pm_metal_coop_mono_us();
        uint64_t wait = next > now ? next - now : 0;
        if (wait > 1000000ull) {
            wait = 1000000ull;
        }
        idle_wait_us(wait);
        stall_until = pm_metal_coop_mono_us() + PM_METAL_COOP_STALL_US;
    }
    if (owner != NULL) {
        owner->on_c_stack = 0;
    }
    return waiter->status == PM_METAL_COOP_DONE ? 0 : -1;
}

int32_t pm_metal_coop_run(pm_metal_coop_task_t *task) {
    if (task == NULL || task->root == NULL) {
        return -1;
    }
    return pm_metal_coop_run_until(task->root);
}

pm_metal_coop_task_t *pm_metal_coop_current_task(void) {
    return *current_slot();
}

uint32_t pm_metal_coop_process_id(void) {
    pm_metal_coop_task_t *t = pm_metal_coop_current_task();
    if (t == NULL) {
        return 0;
    }
    return t->pid;
}

int32_t pm_metal_coop_post_task(pm_metal_coop_task_t *task) {
    if (!s_ready || task == NULL) {
        return -1;
    }
    /* a dead task is never re-posted: the pusher ref is the ref the
     * reclaimer drains — posting one would resurrect dangling blocks */
    return push_task_ref(task);
}

static int32_t pm_metal_coop_boot(pm_util_mem_arena_t *arena) {
    uint32_t n = pm_metal_coop_fill_ncpu();
    if (n == 0u) {
        n = 1u;
    }
    return pm_metal_coop_init(arena, n);
}

/* Strong fill for wasmmod's freestanding io yield hook
 * (ports/freestanding/io_ops.h): a parked fetch checkpoints our runner. */
uint32_t pm_wasmmod_host_io_yield(void) {
    return pm_metal_coop_yield();
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_init, pm_metal_coop_init, int32_t(pm_util_mem_arena_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_deinit, pm_metal_coop_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_arena, pm_metal_coop_arena, pm_util_mem_arena_t *(void));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_mutex_init, pm_metal_coop_mutex_init, void(pm_metal_coop_mutex_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_mutex_try_acquire, pm_metal_coop_mutex_try_acquire, pm_metal_coop_status_t(pm_metal_coop_mutex_t *, pm_metal_coop_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_mutex_release, pm_metal_coop_mutex_release, void(pm_metal_coop_mutex_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_coro_create, pm_metal_coop_coro_create, pm_metal_coop_coro_t *(pm_metal_coop_step_fn, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_create_task, pm_metal_coop_create_task, pm_metal_coop_task_t *(pm_metal_coop_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_await, pm_metal_coop_await, pm_metal_coop_status_t(pm_metal_coop_coro_t *, pm_metal_coop_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_yield_park, pm_metal_coop_yield_park, pm_metal_coop_status_t(pm_metal_coop_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_sleep_us, pm_metal_coop_sleep_us, pm_metal_coop_status_t(pm_metal_coop_coro_t *, uint64_t));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_yield, pm_metal_coop_yield, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_poll, pm_metal_coop_poll, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_run_until, pm_metal_coop_run_until, int32_t(pm_metal_coop_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_run, pm_metal_coop_run, int32_t(pm_metal_coop_task_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_mono_us, pm_metal_coop_mono_us, uint64_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_current_task, pm_metal_coop_current_task, pm_metal_coop_task_t *(void));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_process_id, pm_metal_coop_process_id, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_post_task, pm_metal_coop_post_task, int32_t(pm_metal_coop_task_t *));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_ready, pm_metal_coop_ready, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.coop, pm_metal_coop_n_runners, pm_metal_coop_n_runners, uint32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.coop, pm_metal_coop_boot, pm_metal_coop_deinit);
