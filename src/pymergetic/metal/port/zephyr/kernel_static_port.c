/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/layout.h>
#include <pymergetic/metal/port/kernel_heap_port.h>
#include <pymergetic/metal/port/malloc_heap_port.h>
#include <pymergetic/metal/port/platform.h>

#include <zephyr/kernel.h>

size_t pm_metal_port_kernel_static_bytes(void)
{
	const size_t link = pm_metal_memory_layout_kernel_link();
	const size_t kernel_heap = pm_metal_port_kernel_heap_bytes();

	if (link <= kernel_heap) {
		return 0U;
	}

#if PM_METAL_PORT_IS_FAKE_METAL
	const size_t malloc_heap = pm_metal_port_malloc_heap_bytes();

	if (link <= kernel_heap + malloc_heap) {
		return 0U;
	}

	return link - kernel_heap - malloc_heap;
#else
	return link - kernel_heap;
#endif
}

int pm_metal_port_kernel_static_stats(pm_metal_memory_layout_stats_t *out)
{
	if (out == NULL) {
		return -1;
	}

	out->reserved = pm_metal_port_kernel_static_bytes();
	out->allocated = out->reserved;
	out->free_bytes = 0U;
	return 0;
}

const pm_metal_memory_layout_heap_ops_t pm_metal_port_kernel_static_ops = {
	.bytes = pm_metal_port_kernel_static_bytes,
	.alloc = NULL,
	.free = NULL,
	.realloc = NULL,
	.stats = pm_metal_port_kernel_static_stats,
};
