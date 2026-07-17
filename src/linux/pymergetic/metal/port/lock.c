/*
 * Port — linux bind implementation. pm_metal_port_mutex_t is reinterpreted
 * as pthread_mutex_t in place — see lock.h.
 */
#include "pymergetic/metal/port/lock.h"

#include <pthread.h>
#include <sched.h>

_Static_assert(sizeof(pthread_mutex_t) <= sizeof(pm_metal_port_mutex_t),
	       "pthread_mutex_t does not fit in pm_metal_port_mutex_t storage");

typedef struct pm_metal_port_once_impl {
	/* Plain int + __atomic_*: matches zephyr; avoids C11 atomic_int
	 * clang/clangd "trivially-copyable" diagnostics. */
	int state; /* 0 = pending, 1 = running, 2 = done */
} pm_metal_port_once_impl_t;

_Static_assert(sizeof(pm_metal_port_once_impl_t) <= sizeof(pm_metal_port_once_t),
	       "once_impl does not fit in pm_metal_port_once_t storage");

void pm_metal_port_mutex_init(pm_metal_port_mutex_t *m)
{
	pthread_mutex_init((pthread_mutex_t *)m, NULL);
}

void pm_metal_port_mutex_destroy(pm_metal_port_mutex_t *m)
{
	pthread_mutex_destroy((pthread_mutex_t *)m);
}

void pm_metal_port_mutex_lock(pm_metal_port_mutex_t *m)
{
	pthread_mutex_lock((pthread_mutex_t *)m);
}

void pm_metal_port_mutex_unlock(pm_metal_port_mutex_t *m)
{
	pthread_mutex_unlock((pthread_mutex_t *)m);
}

void pm_metal_port_call_once(pm_metal_port_once_t *once, void (*fn)(void *), void *arg)
{
	pm_metal_port_once_impl_t *o = (pm_metal_port_once_impl_t *)once;
	int expected = 0;

	if (!once || !fn) {
		return;
	}
	if (__atomic_compare_exchange_n(&o->state, &expected, 1, 0, __ATOMIC_SEQ_CST,
					__ATOMIC_SEQ_CST)) {
		fn(arg);
		__atomic_store_n(&o->state, 2, __ATOMIC_SEQ_CST);
		return;
	}
	while (__atomic_load_n(&o->state, __ATOMIC_SEQ_CST) != 2) {
		sched_yield();
	}
}

static void pm_metal_port_mutex_ensure_init(void *arg)
{
	pm_metal_port_mutex_init((pm_metal_port_mutex_t *)arg);
}

void pm_metal_port_mutex_ensure(pm_metal_port_mutex_t *m, pm_metal_port_once_t *once)
{
	if (!m || !once) {
		return;
	}
	pm_metal_port_call_once(once, pm_metal_port_mutex_ensure_init, m);
}
