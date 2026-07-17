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
static int g_pm_metal_memory_bytecode_lock_live;
static pm_metal_port_mutex_t g_pm_metal_memory_bytecode_establish_lock;
static pm_metal_port_once_t g_pm_metal_memory_bytecode_establish_once = PM_METAL_PORT_ONCE_INIT;

static void *pm_metal_memory_zephyr_bytecode_establish(uint64_t requested_bytes,
							uint64_t *out_bytes)
{
	uint64_t take;
	void *block;
	pm_metal_util_arena_t *arena;
	void *ret;

	pm_metal_port_mutex_ensure(&g_pm_metal_memory_bytecode_establish_lock,
				    &g_pm_metal_memory_bytecode_establish_once);
	pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_establish_lock);

	if (g_pm_metal_memory_bytecode_block) {
		if (out_bytes) {
			*out_bytes = g_pm_metal_memory_bytecode_bytes;
		}
		ret = g_pm_metal_memory_bytecode_block;
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_establish_lock);
		return ret;
	}

	take = pm_metal_memory_zephyr_budget_take(requested_bytes);
	if (take == 0) {
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_establish_lock);
		return NULL;
	}

	block = pm_metal_memory_zephyr_pool_alloc(take);
	if (!block) {
		pm_metal_memory_zephyr_budget_give(take);
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_establish_lock);
		return NULL;
	}

	arena = pm_metal_util_arena_init(block, (size_t)take);
	if (!arena) {
		pm_metal_memory_zephyr_pool_free(block);
		pm_metal_memory_zephyr_budget_give(take);
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_establish_lock);
		return NULL;
	}

	pm_metal_port_mutex_init(&g_pm_metal_memory_bytecode_lock);
	g_pm_metal_memory_bytecode_lock_live = 1;

	g_pm_metal_memory_bytecode_block = block;
	g_pm_metal_memory_bytecode_arena = arena;
	g_pm_metal_memory_bytecode_bytes = take;
	if (out_bytes) {
		*out_bytes = take;
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_establish_lock);
	return block;
}

static void pm_metal_memory_zephyr_bytecode_release(void)
{
	void *block;

	pm_metal_port_mutex_ensure(&g_pm_metal_memory_bytecode_establish_lock,
				    &g_pm_metal_memory_bytecode_establish_once);
	pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_establish_lock);

	if (g_pm_metal_memory_bytecode_lock_live) {
		pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_lock);
		g_pm_metal_memory_bytecode_arena = NULL;
		block = g_pm_metal_memory_bytecode_block;
		g_pm_metal_memory_bytecode_block = NULL;
		g_pm_metal_memory_bytecode_bytes = 0;
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_lock);
		pm_metal_port_mutex_destroy(&g_pm_metal_memory_bytecode_lock);
		g_pm_metal_memory_bytecode_lock_live = 0;
	} else {
		block = g_pm_metal_memory_bytecode_block;
		g_pm_metal_memory_bytecode_block = NULL;
		g_pm_metal_memory_bytecode_arena = NULL;
		g_pm_metal_memory_bytecode_bytes = 0;
	}

	pm_metal_memory_zephyr_pool_free(block);
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_establish_lock);
}

static uint64_t pm_metal_memory_zephyr_bytecode_bytes(void)
{
	return g_pm_metal_memory_bytecode_bytes;
}

static void *pm_metal_memory_zephyr_bytecode_alloc(uint32_t size)
{
	void *ptr;

	if (!g_pm_metal_memory_bytecode_lock_live) {
		return NULL;
	}
	pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_lock);
	if (!g_pm_metal_memory_bytecode_arena) {
		pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_lock);
		return NULL;
	}
	ptr = pm_metal_util_arena_alloc(g_pm_metal_memory_bytecode_arena, size);
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_lock);
	return ptr;
}

static void pm_metal_memory_zephyr_bytecode_free(void *ptr)
{
	if (!g_pm_metal_memory_bytecode_lock_live) {
		return;
	}
	pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_lock);
	if (g_pm_metal_memory_bytecode_arena) {
		pm_metal_util_arena_free(g_pm_metal_memory_bytecode_arena, ptr);
	}
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
