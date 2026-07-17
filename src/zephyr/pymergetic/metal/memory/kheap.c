/*
 * Memory — zephyr kheap ops (bind).
 */
#include "pymergetic/metal/memory/kheap.h"

#include <stddef.h>

#include "pymergetic/metal/memory/budget.h"
#include "pymergetic/metal/port/lock.h"

static void *g_pm_metal_memory_kheap_pool;
static uint64_t g_pm_metal_memory_kheap_bytes;
static pm_metal_port_mutex_t g_pm_metal_memory_kheap_establish_lock;
static pm_metal_port_once_t g_pm_metal_memory_kheap_establish_once = PM_METAL_PORT_ONCE_INIT;

static void *pm_metal_memory_zephyr_kheap_establish(uint64_t requested_bytes, uint64_t *out_bytes)
{
	uint64_t take;
	void *pool;
	void *ret;

	pm_metal_port_mutex_ensure(&g_pm_metal_memory_kheap_establish_lock,
				    &g_pm_metal_memory_kheap_establish_once);
	pm_metal_port_mutex_lock(&g_pm_metal_memory_kheap_establish_lock);

	if (g_pm_metal_memory_kheap_pool) {
		if (out_bytes) {
			*out_bytes = g_pm_metal_memory_kheap_bytes;
		}
		ret = g_pm_metal_memory_kheap_pool;
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_kheap_establish_lock);
		return ret;
	}

	take = pm_metal_memory_zephyr_budget_take(requested_bytes);
	if (take == 0) {
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_kheap_establish_lock);
		return NULL;
	}

	pool = pm_metal_memory_zephyr_pool_alloc(take);
	if (!pool) {
		pm_metal_memory_zephyr_budget_give(take);
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_kheap_establish_lock);
		return NULL;
	}

	g_pm_metal_memory_kheap_pool = pool;
	g_pm_metal_memory_kheap_bytes = take;
	if (out_bytes) {
		*out_bytes = take;
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_kheap_establish_lock);
	return pool;
}

static void pm_metal_memory_zephyr_kheap_release(void)
{
	void *pool;

	pm_metal_port_mutex_ensure(&g_pm_metal_memory_kheap_establish_lock,
				    &g_pm_metal_memory_kheap_establish_once);
	pm_metal_port_mutex_lock(&g_pm_metal_memory_kheap_establish_lock);
	pool = g_pm_metal_memory_kheap_pool;
	g_pm_metal_memory_kheap_pool = NULL;
	g_pm_metal_memory_kheap_bytes = 0;
	pm_metal_memory_zephyr_pool_free(pool);
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_kheap_establish_lock);
	/* Do not budget_reset here — bytecode pool may still be alive.
	 * Budget lives until process teardown / both pools released. */
}

static uint64_t pm_metal_memory_zephyr_kheap_bytes(void)
{
	return g_pm_metal_memory_kheap_bytes;
}

static const pm_metal_memory_ops_t g_pm_metal_memory_kheap_ops = {
	.probe = NULL,
	.establish = pm_metal_memory_zephyr_kheap_establish,
	.release = pm_metal_memory_zephyr_kheap_release,
	.bytes = pm_metal_memory_zephyr_kheap_bytes,
	.alloc = NULL,
	.free = NULL,
};

const pm_metal_memory_ops_t *pm_metal_memory_kheap_ops(void)
{
	return &g_pm_metal_memory_kheap_ops;
}
