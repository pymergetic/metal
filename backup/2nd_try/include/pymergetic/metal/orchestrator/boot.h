#ifndef PYMERGETIC_METAL_ORCHESTRATOR_BOOT_H_
#define PYMERGETIC_METAL_ORCHESTRATOR_BOOT_H_

#include <pymergetic/metal/sys/sys.h>
#include <pymergetic/metal/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Upper entry — bootstrap passed in-process from lower (no /sys/pm self-read).
 * Defined: src/pymergetic/metal/orchestrator/boot.c
 */
PM_METAL_KERNEL_API(int, pm_metal_orchestrator_boot,
		    (const pm_metal_sys_bootstrap_t *blob));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ORCHESTRATOR_BOOT_H_ */
