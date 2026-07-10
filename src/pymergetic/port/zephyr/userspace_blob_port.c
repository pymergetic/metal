/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/layout.h>
#include "../headers/userspace_blob.h"
#include "userspace_blob_port.h"

#include <stdlib.h>

#include <zephyr/kernel.h>

size_t pm_metal_port_userspace_blob_bytes(void)
{
	if (!pm_userspace_blob_is_ready()) {
		(void)pm_userspace_blob_init();
	}

	return pm_userspace_blob_total();
}

void *pm_metal_port_userspace_blob_alloc(size_t size)
{
	return malloc(size);
}

void pm_metal_port_userspace_blob_free(void *ptr)
{
	free(ptr);
}

void *pm_metal_port_userspace_blob_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

int pm_metal_port_userspace_blob_stats(pm_metal_memory_layout_stats_t *out)
{
	pm_userspace_blob_stats_t blob_stats;

	if (out == NULL) {
		return -1;
	}

	out->reserved = pm_metal_port_userspace_blob_bytes();
	if (pm_userspace_blob_stats(&blob_stats) != 0) {
		out->allocated = 0U;
		out->free_bytes = out->reserved;
		return 0;
	}

	out->allocated = blob_stats.tlsf_used;
	out->free_bytes = blob_stats.tlsf_free;
	return 0;
}

const pm_metal_memory_layout_heap_ops_t pm_metal_port_userspace_blob_ops = {
	.bytes = pm_metal_port_userspace_blob_bytes,
	.alloc = pm_metal_port_userspace_blob_alloc,
	.free = pm_metal_port_userspace_blob_free,
	.realloc = pm_metal_port_userspace_blob_realloc,
	.stats = pm_metal_port_userspace_blob_stats,
};
