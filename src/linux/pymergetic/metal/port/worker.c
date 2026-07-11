/*
 * Port — linux bind implementation. pm_metal_port_worker_t is
 * reinterpreted as pthread_t in place — see worker.h. A small malloc'd
 * trampoline adapts pm_metal_port_worker_fn's `int` return to pthread's
 * `void *` — nothing reads that return value today (every worker.h caller
 * so far reports its own outcome onto a console sink before returning),
 * but keeping it plumbed through costs nothing and avoids UB from casting
 * away a return type on the pthread side.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* pthread_tryjoin_np() */
#endif
#include "pymergetic/metal/port/worker.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

_Static_assert(sizeof(pthread_t) <= sizeof(pm_metal_port_worker_t),
	       "pthread_t does not fit in pm_metal_port_worker_t storage");

typedef struct pm_metal_port_worker_trampoline_ctx {
	pm_metal_port_worker_fn fn;
	void *arg;
} pm_metal_port_worker_trampoline_ctx_t;

static void *pm_metal_port_worker_trampoline(void *raw)
{
	pm_metal_port_worker_trampoline_ctx_t *tc = raw;
	pm_metal_port_worker_fn fn = tc->fn;
	void *arg = tc->arg;

	free(tc);
	return (void *)(intptr_t)fn(arg);
}

int pm_metal_port_worker_spawn(pm_metal_port_worker_t *w, pm_metal_port_worker_fn fn, void *arg)
{
	pm_metal_port_worker_trampoline_ctx_t *tc = malloc(sizeof(*tc));

	if (!tc) {
		return -1;
	}
	tc->fn = fn;
	tc->arg = arg;

	if (pthread_create((pthread_t *)w, NULL, pm_metal_port_worker_trampoline, tc) != 0) {
		free(tc);
		return -1;
	}
	return 0;
}

int pm_metal_port_worker_try_join(pm_metal_port_worker_t *w)
{
	return pthread_tryjoin_np(*(pthread_t *)w, NULL);
}

void pm_metal_port_worker_join(pm_metal_port_worker_t *w)
{
	pthread_join(*(pthread_t *)w, NULL);
}
