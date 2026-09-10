/* pymergetic.metal.coop — stackless coro/task + lock-free ready ring.
 * Park = return WAITING; frame on util.mem. Not Asyncify. */
#ifndef PYMERGETIC_METAL_COOP_TYPES_H
#define PYMERGETIC_METAL_COOP_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"
#include "pymergetic/util/lock/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_METAL_COOP_PENDING = 0,
    PM_METAL_COOP_WAITING = 1,
    PM_METAL_COOP_DONE = 2,
    PM_METAL_COOP_CANCELLED = 3,
    PM_METAL_COOP_ERROR = 4,
} pm_metal_coop_status_t;

typedef struct pm_metal_coop_coro pm_metal_coop_coro_t;
typedef struct pm_metal_coop_task pm_metal_coop_task_t;
typedef struct pm_metal_coop_mutex pm_metal_coop_mutex_t;

typedef pm_metal_coop_status_t (*pm_metal_coop_step_fn)(pm_metal_coop_coro_t *self);

struct pm_metal_coop_coro {
    pm_metal_coop_step_fn step;
    pm_metal_coop_coro_t *awaiting; /* exclusive 0..1 child; not a ring entry */
    pm_metal_coop_coro_t *waiter;   /* parent */
    pm_metal_coop_task_t *task;     /* outer scheduled unit (set on create_task) */
    uint32_t status;
    uint32_t vm_only; /* 1 = step() re-enters the bytecode VM; stepped under the VM lock */
    uint32_t auto_free; /* 1 = terminal task reclaims this frame too (coro_create block) */
};

struct pm_metal_coop_task {
    pm_metal_coop_coro_t *root;
    uint32_t on_c_stack; /* 1 while nested run_until owns this task's C frame */
    uint32_t running;    /* CAS: one runner steps a task at a time */
    uint32_t pid;        /* 0 = not a process (no human intent / id) */
    pm_metal_coop_task_t *mutex_next; /* intrusive FIFO link while parked on a mutex */
    /* Reclamation bookkeeping. ring_refs counts ready-ring slots holding
     * this task (push +1, the claiming driver's drop -1); dead marks a
     * terminal task whose blocks the last drop frees. A push of a dead
     * task is refused, so the count cannot grow once dead. */
    uint32_t ring_refs;
    uint32_t dead;      /* 1 = terminal + reclaimed: pushes refused, frees on 0 */
};

/* Async-aware mutex: CAS owner, park on contention, wake on release.
 * One mechanism shared by mutex, sem, rwlock, cond — only the predicate
 * around the owner field differs. Never a spin; held across a yield.
 *
 * owner: NULL when unlocked, task pointer when held. CAS directly on this
 * pointer — no separate lock bit. fifo_lock is a raw spinlock that protects
 * only the waiters_head/tail manipulation (nanoseconds, unparkable context). */

struct pm_metal_coop_mutex {
    pm_metal_coop_task_t *owner; /* NULL when unlocked; CAS to claim */
    pm_metal_coop_task_t *waiters_head;
    pm_metal_coop_task_t *waiters_tail;
    pm_util_lock_t fifo_lock; /* protects waiters head/tail only */
    uint32_t _pad;
};

int32_t pm_metal_coop_ready(void);
uint32_t pm_metal_coop_n_runners(void);
const char *pm_metal_coop_runner_kind(void);
uint32_t pm_metal_coop_process_id(void);

/* Mark a coro as re-entering the bytecode VM: any runner core may step it,
 * serialized by the VM lock — not restricted to a single boot-thread slot. */
void pm_metal_coop_coro_set_vm_only(pm_metal_coop_coro_t *coro);

/* Mark a coro's task for auto-reclaim: when the task reaches a terminal
 * status (DONE/CANCELLED/ERROR) and the last ring reference drops, the
 * runner frees BOTH the task block and the coro's frame block (the block
 * pm_metal_coop_coro_create returned — the coro must be its first member
 * and must not be embedded in a larger caller-owned struct). For coros
 * whose frame the caller owns (embedded structs, arena sub-spans), use
 * pm_metal_coop_task_reclaim on the task alone instead. */
void pm_metal_coop_coro_set_auto_free(pm_metal_coop_coro_t *coro);

/* Declare a terminal task's block dead: the runner frees the TASK block
 * (never the coro frame) once the last ring reference drops. The caller
 * must not touch the task afterwards. Refuses (returns -1) unless the
 * task is terminal, so a live task cannot be yanked mid-flight. */
int32_t pm_metal_coop_task_reclaim(pm_metal_coop_task_t *task);

/* Async mutex: park-on-contention, never spin; same cast as sem/rwlock/cond. */
void pm_metal_coop_mutex_init(pm_metal_coop_mutex_t *m);
pm_metal_coop_status_t pm_metal_coop_mutex_try_acquire(pm_metal_coop_mutex_t *m, pm_metal_coop_coro_t *self);
void pm_metal_coop_mutex_release(pm_metal_coop_mutex_t *m);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_COOP_TYPES_H */
