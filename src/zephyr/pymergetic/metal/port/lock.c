/*
 * Port — zephyr bind implementation. pm_metal_port_mutex_t is
 * reinterpreted as struct k_mutex in place — see lock.h. k_mutex has no
 * OS-owned resources to release, so destroy() is a no-op (k_mutex_init()
 * is always safe to call again without it, but we keep the destroy()
 * call at the shared call sites for parity with the linux/pthread impl).
 */
#include "pymergetic/metal/port/lock.h"

#include <zephyr/kernel.h>

_Static_assert(sizeof(struct k_mutex) <= sizeof(pm_metal_port_mutex_t),
	       "struct k_mutex does not fit in pm_metal_port_mutex_t storage");

void pm_metal_port_mutex_init(pm_metal_port_mutex_t *m)
{
	k_mutex_init((struct k_mutex *)m);
}

void pm_metal_port_mutex_destroy(pm_metal_port_mutex_t *m)
{
	(void)m;
}

void pm_metal_port_mutex_lock(pm_metal_port_mutex_t *m)
{
	k_mutex_lock((struct k_mutex *)m, K_FOREVER);
}

void pm_metal_port_mutex_unlock(pm_metal_port_mutex_t *m)
{
	k_mutex_unlock((struct k_mutex *)m);
}
