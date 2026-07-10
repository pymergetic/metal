/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/arena.h>
#include <pymergetic/metal/memory/layout.h>
#include "../../platform/plat.h"

#include <string.h>

int pm_metal_arena_init(void)
{
#if defined(CONFIG_PM_USERSPACE_BLOB)
	const pm_metal_memory_layout_heap_t *heap =
		pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_USERSPACE_BLOB);

	if (heap != NULL && heap->ops != NULL && heap->ops->alloc != NULL) {
		void *probe = heap->ops->alloc(1U);

		if (probe != NULL) {
			heap->ops->free(probe);
		}
	}
#endif
	return 0;
}

int pm_metal_arena_info_get(pm_metal_arena_info_t *out)
{
	pm_metal_memory_layout_t layout;

	if (out == NULL) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	layout = pm_metal_memory_layout_get();

	out->machine_ram = layout.machine_ram;
	out->kernel_link = layout.kernel_link;
	out->capacity = layout.userspace_blob;
	out->used = layout.userspace_malloc_used + layout.userspace_mmap_used;
	out->free_bytes = layout.userspace_pool_free;
	out->malloc_used = layout.userspace_malloc_used;
	out->mmap_used = layout.userspace_mmap_used;
	out->ram_source = pm_plat_machine_ram_source_name(pm_plat_machine_ram_source());

	return 0;
}
