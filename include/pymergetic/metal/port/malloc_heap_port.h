/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PM_METAL_PORT_MALLOC_HEAP_PORT_H_
#define PM_METAL_PORT_MALLOC_HEAP_PORT_H_

#include <pymergetic/metal/memory/layout.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t pm_metal_port_malloc_heap_bytes(void);
void *pm_metal_port_malloc_heap_alloc(size_t size);
void pm_metal_port_malloc_heap_free(void *ptr);
void *pm_metal_port_malloc_heap_realloc(void *ptr, size_t size);
int pm_metal_port_malloc_heap_stats(pm_metal_memory_layout_stats_t *out);

extern const pm_metal_memory_layout_heap_ops_t pm_metal_port_malloc_heap_ops;

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_PORT_MALLOC_HEAP_PORT_H_ */
