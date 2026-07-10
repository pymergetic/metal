/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/layout.h>
#include "kernel_heap_port.h"
#include "kernel_static_port.h"
#include "userspace_blob_port.h"
#include "traits.h"

#include <zephyr/kernel.h>

size_t pm_metal_port_kernel_static_bytes(void)
{
	const size_t link = pm_metal_memory_layout_kernel_link();
	const size_t kernel_heap = pm_metal_port_kernel_heap_bytes();

	if (link <= kernel_heap) {
		return 0U;
	}

#if PM_METAL_PORT_IS_FAKE_METAL
	const size_t userspace_blob = pm_metal_port_userspace_blob_bytes();

	if (link <= kernel_heap + userspace_blob) {
		return 0U;
	}

	return link - kernel_heap - userspace_blob;
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
