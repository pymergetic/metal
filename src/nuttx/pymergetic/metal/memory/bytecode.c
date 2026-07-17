/*
 * Memory — linux bytecode ops (bind).
 *
 * pm_metal_util_arena_* has no locking of its own (see util/arena.h) and
 * alloc()/free() mutate its free-list on every call, so concurrent
 * load()/unload() calls on different handles (runtime.c allows this —
 * see docs/RUNTIME.md "Concurrency") would race on the arena's internal
 * state without a lock here. establish()/release() are called at most
 * once each per init()/shutdown() cycle, from runtime.c's single
 * controller thread (same contract as runtime.c's own lock) — no
 * concurrent access to guard there, only alloc()/free() need it.
 */
#include "pymergetic/metal/memory/bytecode.h"

#include <stdlib.h>

#include "pymergetic/metal/port/lock.h"
#include "pymergetic/metal/util/arena.h"

static void *g_pm_metal_memory_bytecode_block;
static pm_metal_util_arena_t *g_pm_metal_memory_bytecode_arena;
static uint64_t g_pm_metal_memory_bytecode_bytes;
static pm_metal_port_mutex_t g_pm_metal_memory_bytecode_lock;

static void *pm_metal_memory_nuttx_bytecode_establish(uint64_t requested_bytes,
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

	pm_metal_port_mutex_init(&g_pm_metal_memory_bytecode_lock);

	g_pm_metal_memory_bytecode_block = block;
	g_pm_metal_memory_bytecode_arena = arena;
	g_pm_metal_memory_bytecode_bytes = requested_bytes;
	*out_bytes = requested_bytes;

	return block;
}

static void pm_metal_memory_nuttx_bytecode_release(void)
{
	pm_metal_port_mutex_destroy(&g_pm_metal_memory_bytecode_lock);

	free(g_pm_metal_memory_bytecode_block);
	g_pm_metal_memory_bytecode_block = NULL;
	g_pm_metal_memory_bytecode_arena = NULL;
	g_pm_metal_memory_bytecode_bytes = 0;
}

static uint64_t pm_metal_memory_nuttx_bytecode_bytes(void)
{
	return g_pm_metal_memory_bytecode_bytes;
}

static void *pm_metal_memory_nuttx_bytecode_alloc(uint32_t size)
{
	pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_lock);
	void *ptr = pm_metal_util_arena_alloc(g_pm_metal_memory_bytecode_arena, size);
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_lock);

	return ptr;
}

static void pm_metal_memory_nuttx_bytecode_free(void *ptr)
{
	pm_metal_port_mutex_lock(&g_pm_metal_memory_bytecode_lock);
	pm_metal_util_arena_free(g_pm_metal_memory_bytecode_arena, ptr);
	pm_metal_port_mutex_unlock(&g_pm_metal_memory_bytecode_lock);
}

static const pm_metal_memory_ops_t g_pm_metal_memory_bytecode_ops = {
	.probe = NULL,
	.establish = pm_metal_memory_nuttx_bytecode_establish,
	.release = pm_metal_memory_nuttx_bytecode_release,
	.bytes = pm_metal_memory_nuttx_bytecode_bytes,
	.alloc = pm_metal_memory_nuttx_bytecode_alloc,
	.free = pm_metal_memory_nuttx_bytecode_free,
};

const pm_metal_memory_ops_t *pm_metal_memory_bytecode_ops(void)
{
	return &g_pm_metal_memory_bytecode_ops;
}
