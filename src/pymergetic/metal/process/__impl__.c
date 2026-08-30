/* pymergetic.metal.process — crown/spawn/quit over async tasks. REPL is pid 0.
 * Each pid can carry a memory budget: a private sub-arena carved out of the
 * boot arena. The sub-arena's size is the cap — allocations past it return
 * NULL (OOM refusal, not an abort), and quit() returns the whole backing to
 * the boot arena in one shot. */
#include "pymergetic/metal/process/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/util/mem.h"

#include <string.h>

struct slot {
    struct slot *next;
    uint32_t used;
    int32_t pid;
    pm_metal_async_task_t *task;
    pm_util_mem_arena_t *arena; /* NULL until a budget is set */
    void *backing;             /* boot-arena block the sub-arena runs on */
    size_t cap;                /* sub-arena size in bytes (== budget) */
};

static pm_util_mem_arena_t *s_arena;
static struct slot *s_head;
static struct slot *s_tail;
static int32_t s_next_pid = 1;

/* REPL budget (pid 0): the interactive guest can cap its own compiles the
 * same way spawned processes do. Lives outside the slot table because the
 * REPL is not a slot — it is the boot task. */
static pm_util_mem_arena_t *s_repl_arena;
static void *s_repl_backing;
static size_t s_repl_cap;

int32_t pm_metal_process_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_head = NULL;
    s_tail = NULL;
    s_next_pid = 1;
    return 0;
}

void pm_metal_process_deinit(void) {
    struct slot *s = s_head;
    while (s != NULL) {
        struct slot *next = s->next;
        if (s->arena != NULL) {
            pm_util_mem_arena_destroy(s->arena);
            s->arena = NULL;
        }
        if (s->backing != NULL && s_arena != NULL) {
            pm_util_mem_free(s_arena, s->backing);
            s->backing = NULL;
        }
        s = next;
    }
    s_head = NULL;
    s_tail = NULL;
    s_next_pid = 1;
    if (s_repl_arena != NULL) {
        pm_util_mem_arena_destroy(s_repl_arena);
        s_repl_arena = NULL;
    }
    if (s_repl_backing != NULL && s_arena != NULL) {
        pm_util_mem_free(s_arena, s_repl_backing);
        s_repl_backing = NULL;
    }
    s_repl_cap = 0;
    s_arena = NULL;
}

int32_t pm_metal_process_count(void) {
    struct slot *s;
    int32_t n = 0;
    for (s = s_head; s != NULL; s = s->next) {
        if (s->used) {
            n++;
        }
    }
    return n;
}

int32_t pm_metal_process_at(int32_t i) {
    struct slot *s;
    int32_t n = 0;
    if (i < 0) {
        return -1;
    }
    for (s = s_head; s != NULL; s = s->next) {
        if (!s->used) {
            continue;
        }
        if (n == i) {
            return s->pid;
        }
        n++;
    }
    return -1;
}

int32_t pm_metal_process_current(void) {
    return (int32_t)pm_metal_async_process_id();
}

static struct slot *find_slot(int32_t pid) {
    struct slot *s;
    if (pid == 0) {
        pid = pm_metal_process_current();
    }
    if (pid < 1) {
        return NULL;
    }
    for (s = s_head; s != NULL; s = s->next) {
        if (s->used && s->pid == pid) {
            return s;
        }
    }
    return NULL;
}

static void slot_budget_drop(struct slot *s) {
    if (s == NULL || s->backing == NULL) {
        return;
    }
    if (s->arena != NULL) {
        pm_util_mem_arena_destroy(s->arena);
        s->arena = NULL;
    }
    if (s_arena != NULL) {
        pm_util_mem_free(s_arena, s->backing);
    }
    s->backing = NULL;
    s->cap = 0;
}

static int32_t crown_task(pm_metal_async_task_t *t) {
    struct slot *s;
    int32_t pid;
    if (s_arena == NULL || t == NULL || t->pid != 0u) {
        return -1;
    }
    s = (struct slot *)pm_util_mem_alloc(s_arena, sizeof(*s));
    if (s == NULL) {
        return -1;
    }
    pid = s_next_pid++;
    if (pid < 1) {
        s_next_pid = 1;
        pid = s_next_pid++;
    }
    t->pid = (uint32_t)pid;
    s->next = NULL;
    s->used = 1;
    s->pid = pid;
    s->task = t;
    s->arena = NULL;
    s->backing = NULL;
    s->cap = 0;
    if (s_tail == NULL) {
        s_head = s;
    } else {
        s_tail->next = s;
    }
    s_tail = s;
    return pid;
}

int32_t pm_metal_process_crown(void) {
    return crown_task(pm_metal_async_current_task());
}

/* Park once. Do not yield_park (that re-queues and spins the SMP ring). */
static pm_metal_async_status_t idle_step(pm_metal_async_coro_t *self) {
    if (self == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (self->status == PM_METAL_ASYNC_CANCELLED) {
        return PM_METAL_ASYNC_CANCELLED;
    }
    self->status = PM_METAL_ASYNC_WAITING;
    return PM_METAL_ASYNC_WAITING;
}

int32_t pm_metal_process_spawn(void) {
    pm_metal_async_coro_t *c;
    pm_metal_async_task_t *t;
    if (s_arena == NULL || !pm_metal_async_ready()) {
        return -1;
    }
    c = pm_metal_async_coro_create(idle_step, sizeof(*c));
    if (c == NULL) {
        return -1;
    }
    t = pm_metal_async_create_task(c);
    if (t == NULL) {
        return -1;
    }
    return crown_task(t);
}

int32_t pm_metal_process_quit(int32_t pid) {
    struct slot *s;
    if (pid == 0) {
        pid = pm_metal_process_current();
    }
    if (pid < 1) {
        return -1;
    }
    for (s = s_head; s != NULL; s = s->next) {
        if (!s->used || s->pid != pid) {
            continue;
        }
        slot_budget_drop(s);
        if (s->task != NULL) {
            s->task->pid = 0;
            if (s->task->root != NULL) {
                s->task->root->status = PM_METAL_ASYNC_CANCELLED;
            }
        }
        s->used = 0;
        s->pid = 0;
        s->task = NULL;
        return 0;
    }
    return -1;
}

/* Budget faces. cap == 0 means no private budget: the pid shares the boot
 * arena and no limit is enforced for it. pid 0 is the REPL — it can cap
 * itself too (the guest compiles run there). */
int32_t pm_metal_process_budget(int32_t pid) {
    struct slot *s;
    if (pid == 0) {
        return (int32_t)s_repl_cap;
    }
    s = find_slot(pid);
    return (s != NULL) ? (int32_t)s->cap : -1;
}

/* budget_set cap is int32_t on the wire (not size_t): the Python bridge
 * composes i32 sigs only, and every real budget (compile scratch, arenas)
 * is far below 2GB — negative caps are rejected below. */
int32_t pm_metal_process_budget_set(int32_t pid, int32_t cap) {
    struct slot *s = find_slot(pid);
    void *nb;
    if (s_arena == NULL || cap <= 0) {
        return -1;
    }
    if (pid == 0) {
        if (pm_metal_process_current() != 0) {
            return -1; /* only the REPL may set the REPL's budget */
        }
        /* REPL budget: replace the backing, same one-shot posture */
        if ((size_t)cap == s_repl_cap) {
            return 0;
        }
        nb = pm_util_mem_alloc(s_arena, (size_t)cap);
        if (nb == NULL) {
            return -1;
        }
        if (s_repl_arena != NULL) {
            pm_util_mem_arena_destroy(s_repl_arena);
            s_repl_arena = NULL;
        }
        if (s_repl_backing != NULL) {
            pm_util_mem_free(s_arena, s_repl_backing);
        }
        s_repl_backing = nb;
        s_repl_cap = (size_t)cap;
        s_repl_arena = pm_util_mem_arena_create(nb, (size_t)cap);
        if (s_repl_arena == NULL) {
            pm_util_mem_free(s_arena, nb);
            s_repl_backing = NULL;
            s_repl_cap = 0;
            return -1;
        }
        return 0;
    }
    if (s == NULL) {
        return -1;
    }
    if ((size_t)cap == s->cap) {
        return 0; /* already at that budget */
    }
    /* grow/shrink by replacing the backing block: live allocations in the
     * sub-arena are compile scratch, replaced on the next compile — the
     * budget contract is per-process accounting, not live migration */
    nb = pm_util_mem_alloc(s_arena, (size_t)cap);
    if (nb == NULL) {
        return -1;
    }
    slot_budget_drop(s);
    s->backing = nb;
    s->cap = (size_t)cap;
    s->arena = pm_util_mem_arena_create(nb, (size_t)cap);
    if (s->arena == NULL) {
        pm_util_mem_free(s_arena, nb);
        s->backing = NULL;
        s->cap = 0;
        return -1;
    }
    return 0;
}

pm_util_mem_arena_t *pm_metal_process_arena(int32_t pid) {
    struct slot *s;
    if (pid == 0) {
        return s_repl_arena;
    }
    s = find_slot(pid);
    return (s != NULL && s->arena != NULL) ? s->arena : NULL;
}

/* budget_used is int32_t on the wire: same i32-bridge posture as budget_set.
 * A pid past 2GB of live scratch is not a state this card ever reaches. */
int32_t pm_metal_process_budget_used(int32_t pid) {
    struct slot *s;
    if (pid == 0) {
        return (s_repl_arena != NULL)
            ? (int32_t)pm_util_mem_arena_heap_used(s_repl_arena) : 0;
    }
    s = find_slot(pid);
    return (s != NULL && s->arena != NULL)
        ? (int32_t)pm_util_mem_arena_heap_used(s->arena) : 0;
}

void pm_metal_process_reboot(void) {
    pm_metal_boot_shutdown(1, 3u);
}

void pm_metal_process_shutdown(void) {
    pm_metal_boot_shutdown(0, 3u);
}

int32_t pm_metal_process_up(void) {
    int32_t n0;
    int32_t pid;
    if (s_arena == NULL) {
        return -1;
    }
    n0 = pm_metal_process_count();
    if (pm_metal_process_current() != 0) {
        return -1;
    }
    pid = pm_metal_process_spawn();
    if (pid < 1 || pm_metal_process_count() != n0 + 1) {
        return -1;
    }
    pm_metal_async_poll();
    if (pm_metal_process_quit(pid) != 0 || pm_metal_process_count() != n0) {
        return -1;
    }
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_init, pm_metal_process_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_deinit, pm_metal_process_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_count, pm_metal_process_count, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_at, pm_metal_process_at, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_current, pm_metal_process_current, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_crown, pm_metal_process_crown, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_spawn, pm_metal_process_spawn, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_quit, pm_metal_process_quit, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_reboot, pm_metal_process_reboot, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_shutdown, pm_metal_process_shutdown, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_up, pm_metal_process_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_budget, pm_metal_process_budget, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_budget_set, pm_metal_process_budget_set, int32_t(int32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.process, pm_metal_process_budget_used, pm_metal_process_budget_used, int32_t(int32_t));

PM_MOD_BOOT_C(pymergetic.metal.process, pm_metal_process_init, pm_metal_process_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.process, pymergetic.metal.async);
PM_MOD_BOOTDEP_C(pymergetic.metal.process, pymergetic.metal.boot.tree);