/*
 * Runtime boot stamp for /proc/uptime.
 */
#ifndef PYMERGETIC_METAL_MOUNT_PROC_BOOT_H_
#define PYMERGETIC_METAL_MOUNT_PROC_BOOT_H_

#include <stdint.h>

int pm_metal_mount_proc_boot_is_set(void);
uint64_t pm_metal_mount_proc_boot_ms(void);

#endif /* PYMERGETIC_METAL_MOUNT_PROC_BOOT_H_ */
