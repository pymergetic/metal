/*
 * Port — nuttx bind implementation. pm_metal_port_worker_t holds a small
 * heap slot (pthread_t + done flag) rather than a bare pthread_t: NuttX
 * has no pthread_tryjoin_np(), so try_join() polls an atomic done flag
 * set by the trampoline, then pthread_join()s. Same contract as
 * worker.h / the linux bind.
 */
#include "pymergetic/metal/port/worker.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct pm_metal_port_worker_slot {
	pthread_t tid;
	pm_metal_port_worker_fn fn;
	void *arg;
	int done; /* 0 = running, 1 = finished (trampoline set) */
} pm_metal_port_worker_slot_t;

_Static_assert(sizeof(pm_metal_port_worker_slot_t *) <= sizeof(pm_metal_port_worker_t),
	       "worker slot pointer does not fit in pm_metal_port_worker_t");

static void *pm_metal_port_worker_trampoline(void *raw)
{
	pm_metal_port_worker_slot_t *slot = raw;
	pm_metal_port_worker_fn fn = slot->fn;
	void *arg = slot->arg;

	(void)fn(arg);
	__atomic_store_n(&slot->done, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

int pm_metal_port_worker_spawn(pm_metal_port_worker_t *w, pm_metal_port_worker_fn fn, void *arg)
{
	pm_metal_port_worker_slot_t *slot;

	if (!w || !fn) {
		return -1;
	}

	slot = malloc(sizeof(*slot));
	if (!slot) {
		return -1;
	}
	slot->fn = fn;
	slot->arg = arg;
	slot->done = 0;

	if (pthread_create(&slot->tid, NULL, pm_metal_port_worker_trampoline, slot) != 0) {
		free(slot);
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
	if (__atomic_load_n(&slot->done, __ATOMIC_SEQ_CST) == 0) {
		return -1;
	}
	if (pthread_join(slot->tid, NULL) != 0) {
		return -1;
	}
	free(slot);
	*(pm_metal_port_worker_slot_t **)w = NULL;
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
	pthread_join(slot->tid, NULL);
	free(slot);
	*(pm_metal_port_worker_slot_t **)w = NULL;
}
