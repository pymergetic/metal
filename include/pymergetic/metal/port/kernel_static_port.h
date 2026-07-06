/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PM_METAL_PORT_KERNEL_STATIC_PORT_H_
#define PM_METAL_PORT_KERNEL_STATIC_PORT_H_

#include <pymergetic/metal/memory/layout.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t pm_metal_port_kernel_static_bytes(void);
int pm_metal_port_kernel_static_stats(pm_metal_memory_layout_stats_t *out);

extern const pm_metal_memory_layout_heap_ops_t pm_metal_port_kernel_static_ops;

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_PORT_KERNEL_STATIC_PORT_H_ */
