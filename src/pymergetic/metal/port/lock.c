/*
 * Port — freestanding EFI/BIOS UP no-op bind. Cooperative single-threaded
 * hosts; mutex/once are state markers only (see lock.h).
 */
#include "pymergetic/metal/port/lock.h"

typedef struct pm_metal_port_once_impl {
	int state; /* 0 = pending, 1 = running, 2 = done */
} pm_metal_port_once_impl_t;

_Static_assert(sizeof(pm_metal_port_once_impl_t) <= sizeof(pm_metal_port_once_t),
	       "once_impl does not fit in pm_metal_port_once_t storage");

void pm_metal_port_mutex_init(pm_metal_port_mutex_t *m)
{
	(void)m;
}

void pm_metal_port_mutex_destroy(pm_metal_port_mutex_t *m)
{
	(void)m;
}

void pm_metal_port_mutex_lock(pm_metal_port_mutex_t *m)
{
	(void)m;
}

void pm_metal_port_mutex_unlock(pm_metal_port_mutex_t *m)
{
	(void)m;
}

void pm_metal_port_call_once(pm_metal_port_once_t *once, void (*fn)(void *), void *arg)
{
	pm_metal_port_once_impl_t *o = (pm_metal_port_once_impl_t *)once;

	if (!once || !fn) {
		return;
	}
	if (o->state == 2) {
		return;
	}
	o->state = 1;
	fn(arg);
	o->state = 2;
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
