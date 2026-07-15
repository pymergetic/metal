/*
 * tmpfs — impl: zephyr. Stub — real impl needs mount/device.h's own
 * RAMDISK kind (not yet added) composed with fs_mount() littlefs, and is
 * blocked on pymergetic/metal/wasi/file.c's own real os_* backend (also a
 * stub today) — see docs/MOUNT.md "Zephyr prerequisite" for why. Always
 * fails so a "tmpfs" fstab/--mount= line is a normal per-line Stage B
 * failure (logged, skipped) on this target, not a crash or a silent wrong
 * answer.
 */
#include "pymergetic/metal/mount/tmpfs.h"

static int pm_metal_mount_tmpfs_establish(const char *source, const char *opts, char *out_host_path,
					   size_t out_cap)
{
	(void)source;
	(void)opts;
	(void)out_host_path;
	(void)out_cap;
	return -1;
}

static void pm_metal_mount_tmpfs_release(const char *host_path)
{
	(void)host_path;
}

static const pm_metal_mount_ops_t g_pm_metal_mount_tmpfs_ops = {
	.establish = pm_metal_mount_tmpfs_establish,
	.release = pm_metal_mount_tmpfs_release,
};

const pm_metal_mount_ops_t *pm_metal_mount_tmpfs_ops(void)
{
	return &g_pm_metal_mount_tmpfs_ops;
}
