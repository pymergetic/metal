/*
 * Memory — zephyr kheap ops (bind). Stub — probe + arena_budget split
 * pending.
 */
#include "pymergetic/metal/memory/kheap.h"

#include <stddef.h>

static void *pm_metal_memory_zephyr_kheap_establish(uint64_t requested_bytes, uint64_t *out_bytes)
{
	(void)requested_bytes;
	(void)out_bytes;
	return NULL;
}

static void pm_metal_memory_zephyr_kheap_release(void)
{
}

static uint64_t pm_metal_memory_zephyr_kheap_bytes(void)
{
	return 0;
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
