/*
 * Mount device — zephyr bind. RAMDISK = DT zephyr,ram-disk already present;
 * establish() just disk_access_init(name).
 */
#include "pymergetic/metal/mount/device.h"

#include <zephyr/storage/disk_access.h>

int pm_metal_mount_device_establish(pm_metal_mount_device_kind_t kind, const char *opts,
				     pm_metal_mount_device_handle_t *out)
{
	if (!opts || !opts[0] || !out) {
		return -1;
	}
	if (kind != PM_METAL_MOUNT_DEVICE_RAMDISK) {
		return -1;
	}
	if (disk_access_init(opts) != 0) {
		return -1;
	}
	*out = opts;
	return 0;
}

void pm_metal_mount_device_release(pm_metal_mount_device_handle_t handle)
{
	/* Static DT ram-disk — nothing to free. */
	(void)handle;
}

const char *pm_metal_mount_device_disk_name(pm_metal_mount_device_handle_t handle)
{
	return handle;
}
