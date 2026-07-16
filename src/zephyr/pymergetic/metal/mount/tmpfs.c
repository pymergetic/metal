/*
 * tmpfs — impl: zephyr. Composes device.h RAMDISK + fs_mount() littlefs.
 * <source> is the DT disk-name (e.g. "scratch"). Host path is /pm_tmpfs/<source>.
 */
#include "pymergetic/metal/mount/tmpfs.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/disk_access.h>

#include "pymergetic/metal/mount/device.h"
#include "pymergetic/metal/mount/tmpfs_registry.h"

#define PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX 4
/* Flat mount points — Zephyr has no parent dir for nested /pm_tmpfs/<name>. */
#define PM_METAL_MOUNT_TMPFS_MP_PREFIX "/pmt_"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(pm_metal_tmpfs_lfs_0);
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(pm_metal_tmpfs_lfs_1);
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(pm_metal_tmpfs_lfs_2);
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(pm_metal_tmpfs_lfs_3);

static struct fs_mount_t g_pm_metal_tmpfs_mnt[PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX];
static char g_pm_metal_tmpfs_mp[PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX][64];
static char g_pm_metal_tmpfs_disk[PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX][32];
static pm_metal_mount_device_handle_t g_pm_metal_tmpfs_dev[PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX];
static int g_pm_metal_tmpfs_used[PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX];
static struct fs_littlefs *g_pm_metal_tmpfs_cfg[PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX] = {
	&pm_metal_tmpfs_lfs_0,
	&pm_metal_tmpfs_lfs_1,
	&pm_metal_tmpfs_lfs_2,
	&pm_metal_tmpfs_lfs_3,
};

static int pm_metal_mount_tmpfs_slot_alloc(void)
{
	int i;

	for (i = 0; i < PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX; i++) {
		if (!g_pm_metal_tmpfs_used[i]) {
			return i;
		}
	}
	return -1;
}

static int pm_metal_mount_tmpfs_establish(const char *source, const char *opts, char *out_host_path,
					   size_t out_cap)
{
	pm_metal_mount_device_handle_t dev = NULL;
	int slot;
	int rc;
	size_t len;

	if (!source || !source[0] || !out_host_path) {
		return -1;
	}

	if (pm_metal_mount_tmpfs_registry_acquire(source, out_host_path, out_cap) == 0) {
		if (opts && opts[0]) {
			printk("pm_metal_mount: tmpfs: '%s' already established, ignoring options\n",
			       source);
		}
		return 0;
	}

	if (pm_metal_mount_device_establish(PM_METAL_MOUNT_DEVICE_RAMDISK, source, &dev) != 0) {
		printk("pm_metal_mount: tmpfs: device '%s' failed\n", source);
		return -1;
	}

	slot = pm_metal_mount_tmpfs_slot_alloc();
	if (slot < 0) {
		printk("pm_metal_mount: tmpfs: no littlefs slots left for '%s'\n", source);
		pm_metal_mount_device_release(dev);
		return -1;
	}

	snprintf(g_pm_metal_tmpfs_mp[slot], sizeof(g_pm_metal_tmpfs_mp[slot]), "%s%s",
		 PM_METAL_MOUNT_TMPFS_MP_PREFIX, source);
	snprintf(g_pm_metal_tmpfs_disk[slot], sizeof(g_pm_metal_tmpfs_disk[slot]), "%s",
		 pm_metal_mount_device_disk_name(dev));

	/* storage_dev is the disk-access name (matches DT disk-name). */
	static char storage_ids[PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX][32];
	snprintf(storage_ids[slot], sizeof(storage_ids[slot]), "%s",
		 g_pm_metal_tmpfs_disk[slot]);

	g_pm_metal_tmpfs_mnt[slot].type = FS_LITTLEFS;
	g_pm_metal_tmpfs_mnt[slot].mnt_point = g_pm_metal_tmpfs_mp[slot];
	g_pm_metal_tmpfs_mnt[slot].fs_data = g_pm_metal_tmpfs_cfg[slot];
	g_pm_metal_tmpfs_mnt[slot].storage_dev = storage_ids[slot];
	/* RAMDISK is blank every boot. Format first + NO_FORMAT so littlefs
	 * does not probe empty media, log "Corrupted dir pair", and auto-format. */
	g_pm_metal_tmpfs_mnt[slot].flags =
		FS_MOUNT_FLAG_USE_DISK_ACCESS | FS_MOUNT_FLAG_NO_FORMAT;

	rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)storage_ids[slot], NULL,
		     FS_MOUNT_FLAG_USE_DISK_ACCESS);
	if (rc < 0) {
		printk("pm_metal_mount: tmpfs: mkfs failed for '%s' (%d)\n", source, rc);
		pm_metal_mount_device_release(dev);
		return -1;
	}
	rc = fs_mount(&g_pm_metal_tmpfs_mnt[slot]);
	if (rc < 0) {
		printk("pm_metal_mount: tmpfs: mount failed for '%s' (%d)\n", source, rc);
		pm_metal_mount_device_release(dev);
		return -1;
	}

	g_pm_metal_tmpfs_used[slot] = 1;
	/* Durable handle: disk-name buffer outlives caller's source string. */
	g_pm_metal_tmpfs_dev[slot] = g_pm_metal_tmpfs_disk[slot];

	len = strlen(g_pm_metal_tmpfs_mp[slot]);
	if (len + 1 > out_cap) {
		fs_unmount(&g_pm_metal_tmpfs_mnt[slot]);
		g_pm_metal_tmpfs_used[slot] = 0;
		pm_metal_mount_device_release(g_pm_metal_tmpfs_dev[slot]);
		g_pm_metal_tmpfs_dev[slot] = NULL;
		return -1;
	}
	memcpy(out_host_path, g_pm_metal_tmpfs_mp[slot], len + 1);

	if (pm_metal_mount_tmpfs_registry_insert(source, out_host_path) != 0) {
		fs_unmount(&g_pm_metal_tmpfs_mnt[slot]);
		g_pm_metal_tmpfs_used[slot] = 0;
		pm_metal_mount_device_release(g_pm_metal_tmpfs_dev[slot]);
		g_pm_metal_tmpfs_dev[slot] = NULL;
		return -1;
	}

	(void)opts;
	return 0;
}

static void pm_metal_mount_tmpfs_release(const char *host_path)
{
	int rc = pm_metal_mount_tmpfs_registry_release(host_path);
	int i;

	if (rc == 1) {
		for (i = 0; i < PM_METAL_MOUNT_TMPFS_ZEPHYR_MAX; i++) {
			if (g_pm_metal_tmpfs_used[i]
			    && strcmp(g_pm_metal_tmpfs_mp[i], host_path) == 0) {
				fs_unmount(&g_pm_metal_tmpfs_mnt[i]);
				g_pm_metal_tmpfs_used[i] = 0;
				pm_metal_mount_device_release(g_pm_metal_tmpfs_dev[i]);
				g_pm_metal_tmpfs_dev[i] = NULL;
				break;
			}
		}
	} else if (rc < 0) {
		printk("pm_metal_mount: tmpfs: release of untracked path %s\n", host_path);
	}
}

static const pm_metal_mount_ops_t g_pm_metal_mount_tmpfs_ops = {
	.establish = pm_metal_mount_tmpfs_establish,
	.release = pm_metal_mount_tmpfs_release,
};

const pm_metal_mount_ops_t *pm_metal_mount_tmpfs_ops(void)
{
	return &g_pm_metal_mount_tmpfs_ops;
}
