/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/layout.h>
#include <pymergetic/metal/port/malloc_heap_port.h>
#include <pymergetic/metal/port/platform.h>

#include <stdlib.h>

#include <zephyr/kernel.h>

#if defined(CONFIG_COMMON_LIBC_MALLOC) && defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
#include <sys_malloc.h>
#endif

size_t pm_metal_port_malloc_heap_bytes(void)
{
#if PM_METAL_PORT_IS_FAKE_METAL
	__ASSERT(CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE > 0,
		 "native_sim requires in-link CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE");
	return (size_t)CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE;
#else
	return pm_metal_memory_layout_sram_tail();
#endif
}

void *pm_metal_port_malloc_heap_alloc(size_t size)
{
	return malloc(size);
}

void pm_metal_port_malloc_heap_free(void *ptr)
{
	free(ptr);
}

void *pm_metal_port_malloc_heap_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

int pm_metal_port_malloc_heap_stats(pm_metal_memory_layout_stats_t *out)
{
#if defined(CONFIG_COMMON_LIBC_MALLOC) && defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
	struct sys_memory_stats zstats;

	if (out == NULL) {
		return -1;
	}

	out->reserved = pm_metal_port_malloc_heap_bytes();
	if (malloc_runtime_stats_get(&zstats) != 0) {
		return -1;
	}

	out->allocated = zstats.allocated_bytes;
	out->free_bytes = zstats.free_bytes;
	return 0;
#else
	if (out == NULL) {
		return -1;
	}

	out->reserved = pm_metal_port_malloc_heap_bytes();
	out->allocated = 0U;
	out->free_bytes = out->reserved;
	return 0;
#endif
}

const pm_metal_memory_layout_heap_ops_t pm_metal_port_malloc_heap_ops = {
	.bytes = pm_metal_port_malloc_heap_bytes,
	.alloc = pm_metal_port_malloc_heap_alloc,
	.free = pm_metal_port_malloc_heap_free,
	.realloc = pm_metal_port_malloc_heap_realloc,
	.stats = pm_metal_port_malloc_heap_stats,
};
