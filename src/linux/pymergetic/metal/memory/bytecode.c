/*
 * Memory — linux bytecode ops (bind).
 */
#include "pymergetic/metal/memory/bytecode.h"

#include <stdlib.h>

#include "pymergetic/metal/util/arena.h"

static void *g_pm_metal_memory_bytecode_block;
static pm_metal_util_arena_t *g_pm_metal_memory_bytecode_arena;
static uint64_t g_pm_metal_memory_bytecode_bytes;

static void *pm_metal_memory_linux_bytecode_establish(uint64_t requested_bytes,
						       uint64_t *out_bytes)
{
	void *block = malloc((size_t)requested_bytes);

	if (!block) {
		return NULL;
	}

	pm_metal_util_arena_t *arena = pm_metal_util_arena_init(block, (size_t)requested_bytes);

	if (!arena) {
		free(block);
		return NULL;
	}

	g_pm_metal_memory_bytecode_block = block;
	g_pm_metal_memory_bytecode_arena = arena;
	g_pm_metal_memory_bytecode_bytes = requested_bytes;
	*out_bytes = requested_bytes;

	return block;
}

static void pm_metal_memory_linux_bytecode_release(void)
{
	free(g_pm_metal_memory_bytecode_block);
	g_pm_metal_memory_bytecode_block = NULL;
	g_pm_metal_memory_bytecode_arena = NULL;
	g_pm_metal_memory_bytecode_bytes = 0;
}

static uint64_t pm_metal_memory_linux_bytecode_bytes(void)
{
	return g_pm_metal_memory_bytecode_bytes;
}

static void *pm_metal_memory_linux_bytecode_alloc(uint32_t size)
{
	return pm_metal_util_arena_alloc(g_pm_metal_memory_bytecode_arena, size);
}

static void pm_metal_memory_linux_bytecode_free(void *ptr)
{
	pm_metal_util_arena_free(g_pm_metal_memory_bytecode_arena, ptr);
}

static const pm_metal_memory_ops_t g_pm_metal_memory_bytecode_ops = {
	.probe = NULL,
	.establish = pm_metal_memory_linux_bytecode_establish,
	.release = pm_metal_memory_linux_bytecode_release,
	.bytes = pm_metal_memory_linux_bytecode_bytes,
	.alloc = pm_metal_memory_linux_bytecode_alloc,
	.free = pm_metal_memory_linux_bytecode_free,
};

const pm_metal_memory_ops_t *pm_metal_memory_bytecode_ops(void)
{
	return &g_pm_metal_memory_bytecode_ops;
}
