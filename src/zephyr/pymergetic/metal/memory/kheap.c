/*
 * Memory — zephyr kheap ops (bind).
 */
#include "pymergetic/metal/memory/kheap.h"

#include <stddef.h>

#include "pymergetic/metal/memory/budget.h"

static void *g_pm_metal_memory_kheap_pool;
static uint64_t g_pm_metal_memory_kheap_bytes;

static void *pm_metal_memory_zephyr_kheap_establish(uint64_t requested_bytes, uint64_t *out_bytes)
{
	uint64_t take;
	void *pool;

	take = pm_metal_memory_zephyr_budget_take(requested_bytes);
	if (take == 0) {
		return NULL;
	}

	pool = pm_metal_memory_zephyr_pool_alloc(take);
	if (!pool) {
		return NULL;
	}

	g_pm_metal_memory_kheap_pool = pool;
	g_pm_metal_memory_kheap_bytes = take;
	*out_bytes = take;
	return pool;
}

static void pm_metal_memory_zephyr_kheap_release(void)
{
	pm_metal_memory_zephyr_pool_free(g_pm_metal_memory_kheap_pool);
	g_pm_metal_memory_kheap_pool = NULL;
	g_pm_metal_memory_kheap_bytes = 0;
	pm_metal_memory_zephyr_budget_reset();
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
