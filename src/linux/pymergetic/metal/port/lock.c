/*
 * Port — linux bind implementation. pm_metal_port_mutex_t is reinterpreted
 * as pthread_mutex_t in place — see lock.h.
 */
#include "pymergetic/metal/port/lock.h"

#include <pthread.h>

_Static_assert(sizeof(pthread_mutex_t) <= sizeof(pm_metal_port_mutex_t),
	       "pthread_mutex_t does not fit in pm_metal_port_mutex_t storage");

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
