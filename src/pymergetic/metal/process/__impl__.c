/* pymergetic.metal.process — crown/spawn/quit over async tasks. REPL is pid 0. */
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
};

static pm_util_mem_arena_t *s_arena;
static struct slot *s_head;
static struct slot *s_tail;
static int32_t s_next_pid = 1;

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
    s_head = NULL;
    s_tail = NULL;
    s_next_pid = 1;
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

void pm_metal_process_reboot(void) {
    pm_metal_boot_shutdown(1);
}

void pm_metal_process_shutdown(void) {
    pm_metal_boot_shutdown(0);
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

PM_MOD_BOOT_C(pymergetic.metal.process, pm_metal_process_init, pm_metal_process_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.process, pymergetic.metal.async);
PM_MOD_BOOTDEP_C(pymergetic.metal.process, pymergetic.metal.boot.tree);
