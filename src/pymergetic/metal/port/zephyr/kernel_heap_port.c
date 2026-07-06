/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/port/kernel_heap_port.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

#if K_HEAP_MEM_POOL_SIZE > 0
extern struct k_heap _system_heap;
#endif

size_t pm_metal_port_kernel_heap_bytes(void)
{
	return (size_t)CONFIG_HEAP_MEM_POOL_SIZE;
}

void *pm_metal_port_kernel_heap_alloc(size_t size)
{
#if K_HEAP_MEM_POOL_SIZE > 0
	return k_malloc(size);
#else
	ARG_UNUSED(size);
	return NULL;
#endif
}

void pm_metal_port_kernel_heap_free(void *ptr)
{
#if K_HEAP_MEM_POOL_SIZE > 0
	k_free(ptr);
#else
	ARG_UNUSED(ptr);
#endif
}

void *pm_metal_port_kernel_heap_realloc(void *ptr, size_t size)
{
	ARG_UNUSED(ptr);
	ARG_UNUSED(size);
	return NULL;
}

int pm_metal_port_kernel_heap_stats(pm_metal_memory_layout_stats_t *out)
{
#if K_HEAP_MEM_POOL_SIZE > 0 && defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
	struct sys_memory_stats zstats;

	if (out == NULL) {
		return -1;
	}

	out->reserved = pm_metal_port_kernel_heap_bytes();
	if (sys_heap_runtime_stats_get(&_system_heap.heap, &zstats) != 0) {
		return -1;
	}

	out->allocated = zstats.allocated_bytes;
	out->free_bytes = zstats.free_bytes;
	return 0;
#else
	if (out == NULL) {
		return -1;
	}

	out->reserved = pm_metal_port_kernel_heap_bytes();
	out->allocated = 0U;
	out->free_bytes = out->reserved;
	return 0;
#endif
}

const pm_metal_memory_layout_heap_ops_t pm_metal_port_kernel_heap_ops = {
	.bytes = pm_metal_port_kernel_heap_bytes,
	.alloc = pm_metal_port_kernel_heap_alloc,
	.free = pm_metal_port_kernel_heap_free,
	.realloc = pm_metal_port_kernel_heap_realloc,
	.stats = pm_metal_port_kernel_heap_stats,
};
