/*
 * Memory — zephyr bytecode ops (bind).
 * Second slice of arena_budget; alloc/free mutex-guarded like linux.
 */
#include "pymergetic/metal/memory/bytecode.h"

#include <stddef.h>

#include "pymergetic/metal/memory/budget.h"
#include "pymergetic/metal/port/lock.h"
#include "pymergetic/metal/util/arena.h"

static void *g_pm_metal_memory_bytecode_block;
static pm_metal_util_arena_t *g_pm_metal_memory_bytecode_arena;
static uint64_t g_pm_metal_memory_bytecode_bytes;
static pm_metal_port_mutex_t g_pm_metal_memory_bytecode_lock;

static void *pm_metal_memory_zephyr_bytecode_establish(uint64_t requested_bytes,
							uint64_t *out_bytes)
{
	uint64_t take;
	void *block;
	pm_metal_util_arena_t *arena;

	take = pm_metal_memory_zephyr_budget_take(requested_bytes);
	if (take == 0) {
		return NULL;
	}

	block = pm_metal_memory_zephyr_pool_alloc(take);
	if (!block) {
		return NULL;
	}

	arena = pm_metal_util_arena_init(block, (size_t)take);
	if (!arena) {
		pm_metal_memory_zephyr_pool_free(block);
		return NULL;
	}

	pm_metal_port_mutex_init(&g_pm_metal_memory_bytecode_lock);

	g_pm_metal_memory_bytecode_block = block;
	g_pm_metal_memory_bytecode_arena = arena;
	g_pm_metal_memory_bytecode_bytes = take;
	*out_bytes = take;
	return block;
}

static void pm_metal_memory_zephyr_bytecode_release(void)
{
	pm_metal_port_mutex_destroy(&g_pm_metal_memory_bytecode_lock);

	pm_metal_memory_zephyr_pool_free(g_pm_metal_memory_bytecode_block);
	g_pm_metal_memory_bytecode_block = NULL;
	g_pm_metal_memory_bytecode_arena = NULL;
	g_pm_metal_memory_bytecode_bytes = 0;
}

static uint64_t pm_metal_memory_zephyr_bytecode_bytes(void)
{
	return g_pm_metal_memory_bytecode_bytes;
}

static void *pm_metal_memory_zephyr_bytecode_alloc(uint32_t size)
{
	void *ptr;

	pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_lock);
	ptr = pm_metal_util_arena_alloc(g_pm_metal_memory_bytecode_arena, size);
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_lock);
	return ptr;
}

static void pm_metal_memory_zephyr_bytecode_free(void *ptr)
{
	pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_lock);
	pm_metal_util_arena_free(g_pm_metal_memory_bytecode_arena, ptr);
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_lock);
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
