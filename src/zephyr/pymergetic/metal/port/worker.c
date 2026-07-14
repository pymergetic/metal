/*
 * Port — zephyr bind. Stub — deferred (see docs/RUNTIME.md "Bring-up
 * plan" §5). A real implementation
 * would reinterpret pm_metal_port_worker_t as a struct k_thread (plus its
 * own stack, unlike pthread — k_thread_create() takes an explicit stack
 * buffer/size the caller must own, so the real signature here will likely
 * need to grow beyond this stub's).
 */
#include "pymergetic/metal/port/worker.h"

int pm_metal_port_worker_spawn(pm_metal_port_worker_t *w, pm_metal_port_worker_fn fn, void *arg)
{
	(void)w;
	(void)fn;
	(void)arg;
	return -1;
}

int pm_metal_port_worker_try_join(pm_metal_port_worker_t *w)
{
	(void)w;
	return -1;
}

void pm_metal_port_worker_join(pm_metal_port_worker_t *w)
{
	(void)w;
}
