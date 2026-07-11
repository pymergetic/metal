/*
 * Shared lower runtime entry — symmetric boot sequence for host/linux and host/zephyr.
 *
 * Defined: host/common/pymergetic/metal/runtime/entry.c
 */
#ifndef PYMERGETIC_METAL_RUNTIME_ENTRY_H_
#define PYMERGETIC_METAL_RUNTIME_ENTRY_H_

#include <pymergetic/metal/export.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_runtime_config {
	const char *target;
	const char *handoff_vfs_root;
} pm_metal_runtime_config_t;

PM_METAL_KERNEL_API(int, pm_metal_runtime_main, (const pm_metal_runtime_config_t *cfg));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_RUNTIME_ENTRY_H_ */
