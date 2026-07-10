/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unified userspace memory for consumers on all targets.
 *
 * Allocation: use libc malloc/calloc/realloc/free and POSIX mmap/munmap
 * (anonymous). Both route through one arena on every target.
 *
 * Introspection only in this header — no second allocator API.
 */

#ifndef PM_METAL_MEMORY_ARENA_H_
#define PM_METAL_MEMORY_ARENA_H_

#include <pymergetic/pm_vis.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if PM_MAX_VIS >= PM_VIS_RUNTIME

typedef struct pm_metal_arena_info {
	size_t machine_ram;
	size_t kernel_link;
	size_t capacity;
	size_t used;
	size_t free_bytes;
	size_t malloc_used;
	size_t mmap_used;
	const char *ram_source;
} pm_metal_arena_info_t;

PM_API(PM_VIS_RUNTIME, int, pm_metal_arena_init, (void))
PM_API(PM_VIS_RUNTIME, int, pm_metal_arena_info_get, (pm_metal_arena_info_t *out))

#endif /* PM_MAX_VIS >= PM_VIS_RUNTIME */

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_MEMORY_ARENA_H_ */
