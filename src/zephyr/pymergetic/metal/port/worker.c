/*
 * Port — zephyr worker bind. k_thread + owned stack in the opaque worker
 * storage (stack lives in a side table keyed by worker pointer).
 */
#include "pymergetic/metal/port/worker.h"

#include <zephyr/kernel.h>

#define PM_METAL_PORT_WORKER_STACK_SIZE 65536
#define PM_METAL_PORT_WORKER_MAX 8

typedef struct pm_metal_port_worker_slot {
	struct k_thread thread;
	k_thread_stack_t *stack;
	k_tid_t tid;
	pm_metal_port_worker_fn fn;
	void *arg;
	int done;
	int result;
	struct k_sem done_sem;
	int used;
} pm_metal_port_worker_slot_t;

K_THREAD_STACK_ARRAY_DEFINE(g_pm_metal_port_worker_stacks, PM_METAL_PORT_WORKER_MAX,
			     PM_METAL_PORT_WORKER_STACK_SIZE);

static pm_metal_port_worker_slot_t g_pm_metal_port_worker_slots[PM_METAL_PORT_WORKER_MAX];

K_MUTEX_DEFINE(g_pm_metal_port_worker_table_lock);

_Static_assert(sizeof(pm_metal_port_worker_slot_t *) <= sizeof(pm_metal_port_worker_t),
	       "worker slot pointer does not fit in pm_metal_port_worker_t");

static void pm_metal_port_worker_entry(void *p1, void *p2, void *p3)
{
	pm_metal_port_worker_slot_t *slot = p1;

	(void)p2;
	(void)p3;
	slot->result = slot->fn(slot->arg);
	slot->done = 1;
	k_sem_give(&slot->done_sem);
}

static pm_metal_port_worker_slot_t *pm_metal_port_worker_slot_alloc(void)
{
	int i;

	k_mutex_lock(&g_pm_metal_port_worker_table_lock, K_FOREVER);
	for (i = 0; i < PM_METAL_PORT_WORKER_MAX; i++) {
		if (!g_pm_metal_port_worker_slots[i].used) {
			g_pm_metal_port_worker_slots[i].used = 1;
			g_pm_metal_port_worker_slots[i].stack = g_pm_metal_port_worker_stacks[i];
			k_mutex_unlock(&g_pm_metal_port_worker_table_lock);
			return &g_pm_metal_port_worker_slots[i];
		}
	}
	k_mutex_unlock(&g_pm_metal_port_worker_table_lock);
	return NULL;
}

static void pm_metal_port_worker_slot_free(pm_metal_port_worker_slot_t *slot)
{
	k_mutex_lock(&g_pm_metal_port_worker_table_lock, K_FOREVER);
	slot->used = 0;
	k_mutex_unlock(&g_pm_metal_port_worker_table_lock);
}

int pm_metal_port_worker_spawn(pm_metal_port_worker_t *w, pm_metal_port_worker_fn fn, void *arg)
{
	pm_metal_port_worker_slot_t *slot;

	if (!w || !fn) {
		return -1;
	}

	slot = pm_metal_port_worker_slot_alloc();
	if (!slot) {
		return -1;
	}

	slot->fn = fn;
	slot->arg = arg;
	slot->done = 0;
	slot->result = 0;
	k_sem_init(&slot->done_sem, 0, 1);

	slot->tid = k_thread_create(&slot->thread, slot->stack, PM_METAL_PORT_WORKER_STACK_SIZE,
				     pm_metal_port_worker_entry, slot, NULL, NULL,
				     /* Same priority as main so k_yield()/timeslicing
				      * can preempt a busy wasm loop (native_sim); a
				      * lower-priority worker is starved forever under
				      * a hot guest, and kill()/wait() never run. */
				     K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
	if (!slot->tid) {
		pm_metal_port_worker_slot_free(slot);
		return -1;
	}

	*(pm_metal_port_worker_slot_t **)w = slot;
	return 0;
}

int pm_metal_port_worker_try_join(pm_metal_port_worker_t *w)
{
	pm_metal_port_worker_slot_t *slot;

	if (!w) {
		return -1;
	}
	slot = *(pm_metal_port_worker_slot_t **)w;
	if (!slot) {
		return -1;
	}
	/* Sem take is the finished check — avoids racing a plain done flag. */
	if (k_sem_take(&slot->done_sem, K_NO_WAIT) != 0) {
		return -1;
	}
	k_thread_join(slot->tid, K_NO_WAIT);
	pm_metal_port_worker_slot_free(slot);
	return 0;
}

void pm_metal_port_worker_join(pm_metal_port_worker_t *w)
{
	pm_metal_port_worker_slot_t *slot;

	if (!w) {
		return;
	}
	slot = *(pm_metal_port_worker_slot_t **)w;
	if (!slot) {
		return;
	}
	k_sem_take(&slot->done_sem, K_FOREVER);
	k_thread_join(slot->tid, K_FOREVER);
	pm_metal_port_worker_slot_free(slot);
}
