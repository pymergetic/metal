/*
 * Mount device layer — zephyr block-device ops (RAMDISK now).
 * See docs/MOUNT.md "Block device layer".
 */
#ifndef PYMERGETIC_METAL_MOUNT_DEVICE_H_
#define PYMERGETIC_METAL_MOUNT_DEVICE_H_

#include <stddef.h>

typedef enum pm_metal_mount_device_kind {
	PM_METAL_MOUNT_DEVICE_RAMDISK = 0,
	/* Phase 6: PARTITION, IMAGE */
} pm_metal_mount_device_kind_t;

/* Opaque handle — for RAMDISK this is the disk-name string pointer owned
 * by the bind (stable for process lifetime). */
typedef const char *pm_metal_mount_device_handle_t;

/*
 * impl: bind — src/zephyr/pymergetic/metal/mount/device.c
 * not impl: bind — src/linux/pymergetic/metal/mount/device.c
 *   (linux tmpfs delegates to host tmpfs directly, no device step)
 *
 * Establish a device of `kind`. For RAMDISK, `opts` is the disk-name
 * (e.g. "scratch") matching a DT zephyr,ram-disk disk-name. Returns 0/-1;
 * on success *out is a handle for release() / fs_mount.
 */
int pm_metal_mount_device_establish(pm_metal_mount_device_kind_t kind, const char *opts,
				     pm_metal_mount_device_handle_t *out);

/* impl: bind — src/zephyr/pymergetic/metal/mount/device.c
 * not impl: bind — src/linux/pymergetic/metal/mount/device.c */
void pm_metal_mount_device_release(pm_metal_mount_device_handle_t handle);

/* Disk name string for a RAMDISK handle (same pointer as handle today).
 * impl: bind — src/zephyr/pymergetic/metal/mount/device.c
 * not impl: bind — src/linux/pymergetic/metal/mount/device.c */
const char *pm_metal_mount_device_disk_name(pm_metal_mount_device_handle_t handle);

#endif /* PYMERGETIC_METAL_MOUNT_DEVICE_H_ */
