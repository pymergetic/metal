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

typedef struct pm_metal_port_once_impl {
	/* Plain int + __atomic_*: C11 atomic_int trips some clang/clangd
	 * front-ends ("not trivially-copyable") on this target. */
	int state; /* 0 = pending, 1 = running, 2 = done */
} pm_metal_port_once_impl_t;

_Static_assert(sizeof(pm_metal_port_once_impl_t) <= sizeof(pm_metal_port_once_t),
	       "once_impl does not fit in pm_metal_port_once_t storage");

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
		k_yield();
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
