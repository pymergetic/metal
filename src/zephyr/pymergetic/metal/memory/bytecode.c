/*
 * Memory — zephyr bytecode ops (bind). Stub — second slice of the same
 * arena_budget remainder pending.
 */
#include "pymergetic/metal/memory/bytecode.h"

#include <stddef.h>

static void *pm_metal_memory_zephyr_bytecode_establish(uint64_t requested_bytes,
							uint64_t *out_bytes)
{
	(void)requested_bytes;
	(void)out_bytes;
	return NULL;
}

static void pm_metal_memory_zephyr_bytecode_release(void)
{
}

static uint64_t pm_metal_memory_zephyr_bytecode_bytes(void)
{
	return 0;
}

static void *pm_metal_memory_zephyr_bytecode_alloc(uint32_t size)
{
	(void)size;
	return NULL;
}

static void pm_metal_memory_zephyr_bytecode_free(void *ptr)
{
	(void)ptr;
}

static const pm_metal_memory_ops_t g_pm_metal_memory_bytecode_ops = {
	.probe = NULL,
	.establish = pm_metal_memory_zephyr_bytecode_establish,
	.release = pm_metal_memory_zephyr_bytecode_release,
	.bytes = pm_metal_memory_zephyr_bytecode_bytes,
	.alloc = pm_metal_memory_zephyr_bytecode_alloc,
	.free = pm_metal_memory_zephyr_bytecode_free,
};

const pm_metal_memory_ops_t *pm_metal_memory_bytecode_ops(void)
{
	return &g_pm_metal_memory_bytecode_ops;
}
