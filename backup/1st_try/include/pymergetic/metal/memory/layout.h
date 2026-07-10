/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Machine RAM, link map, heap slots, and layout snapshot.
 *
 * Hybrid interface: free functions are canonical; pm_metal_memory_layout_ops and
 * pm_metal_memory_layout_heap_ops re-export those implementations as vtables.
 */

#ifndef PM_METAL_MEMORY_LAYOUT_H_
#define PM_METAL_MEMORY_LAYOUT_H_

#include <pymergetic/pm_vis.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if PM_MAX_VIS >= PM_VIS_RUNTIME

typedef struct pm_metal_memory_layout pm_metal_memory_layout_t;

typedef struct pm_metal_memory_layout_ops {
	size_t (*machine_ram)(void);
	size_t (*kernel_link)(void);
	void (*report)(const pm_metal_memory_layout_t *layout);
} pm_metal_memory_layout_ops_t;

typedef enum pm_metal_memory_layout_slot {
	PM_METAL_MEMORY_LAYOUT_SLOT_KERNEL_STATIC = 0,
	PM_METAL_MEMORY_LAYOUT_SLOT_KERNEL_HEAP,
	PM_METAL_MEMORY_LAYOUT_SLOT_USERSPACE_BLOB,
	PM_METAL_MEMORY_LAYOUT_SLOT_COUNT,
} pm_metal_memory_layout_slot_t;

typedef struct pm_metal_memory_layout_stats {
	size_t reserved;
	size_t allocated;
	size_t free_bytes;
} pm_metal_memory_layout_stats_t;

typedef struct pm_metal_memory_layout_heap_ops {
	size_t (*bytes)(void);
	void *(*alloc)(size_t size);
	void (*free)(void *ptr);
	void *(*realloc)(void *ptr, size_t size);
	int (*stats)(pm_metal_memory_layout_stats_t *out);
} pm_metal_memory_layout_heap_ops_t;

typedef struct pm_metal_memory_layout_heap {
	pm_metal_memory_layout_slot_t slot;
	const char *name;
	const pm_metal_memory_layout_heap_ops_t *ops;
} pm_metal_memory_layout_heap_t;

struct pm_metal_memory_layout {
	size_t machine_ram;
	size_t kernel_static;
	size_t kernel_heap;
	size_t userspace_blob;
	size_t userspace_malloc_used;
	size_t userspace_mmap_used;
	size_t userspace_pool_free;
	size_t kernel_link;
};

PM_DECL(PM_VIS_RUNTIME, extern const pm_metal_memory_layout_ops_t pm_metal_memory_layout_ops)

PM_API(PM_VIS_RUNTIME, size_t, pm_metal_memory_layout_machine_ram, (void))
PM_API(PM_VIS_RUNTIME, size_t, pm_metal_memory_layout_kernel_link, (void))
PM_API(PM_VIS_RUNTIME, size_t, pm_metal_memory_layout_sram_tail, (void))

PM_API(PM_VIS_RUNTIME, pm_metal_memory_layout_t, pm_metal_memory_layout_get, (void))
PM_API(PM_VIS_RUNTIME, void, pm_metal_memory_layout_report, (const pm_metal_memory_layout_t *layout))

PM_API(PM_VIS_RUNTIME, const pm_metal_memory_layout_heap_t *, pm_metal_memory_layout_heap_get,
      (pm_metal_memory_layout_slot_t slot))
PM_API(PM_VIS_RUNTIME, size_t, pm_metal_memory_layout_heap_bytes, (const pm_metal_memory_layout_heap_t *heap))
PM_API(PM_VIS_RUNTIME, int, pm_metal_memory_layout_heap_stats,
      (const pm_metal_memory_layout_heap_t *heap, pm_metal_memory_layout_stats_t *out))

#endif /* PM_MAX_VIS >= PM_VIS_RUNTIME */

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_MEMORY_LAYOUT_H_ */
