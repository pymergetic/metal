/*
 * Memory — linux kheap ops (bind).
 */
#include "pymergetic/metal/memory/kheap.h"

#include <stdlib.h>

static void *g_pm_metal_memory_kheap_pool;
static uint64_t g_pm_metal_memory_kheap_bytes;

static void *pm_metal_memory_linux_kheap_establish(uint64_t requested_bytes, uint64_t *out_bytes)
{
	void *pool = malloc((size_t)requested_bytes);

	if (!pool) {
		return NULL;
	}

	g_pm_metal_memory_kheap_pool = pool;
	g_pm_metal_memory_kheap_bytes = requested_bytes;
	*out_bytes = requested_bytes;

	return pool;
}

static void pm_metal_memory_linux_kheap_release(void)
{
	free(g_pm_metal_memory_kheap_pool);
	g_pm_metal_memory_kheap_pool = NULL;
	g_pm_metal_memory_kheap_bytes = 0;
}

static uint64_t pm_metal_memory_linux_kheap_bytes(void)
{
	return g_pm_metal_memory_kheap_bytes;
}

static const pm_metal_memory_ops_t g_pm_metal_memory_kheap_ops = {
	.probe = NULL,
	.establish = pm_metal_memory_linux_kheap_establish,
	.release = pm_metal_memory_linux_kheap_release,
	.bytes = pm_metal_memory_linux_kheap_bytes,
	.alloc = NULL,
	.free = NULL,
};

const pm_metal_memory_ops_t *pm_metal_memory_kheap_ops(void)
{
	return &g_pm_metal_memory_kheap_ops;
}
