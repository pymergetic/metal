/*
 * SPDX-License-Identifier: Apache-2.0
 * Zephyr firmware — publish bootstrap via Zephyr VFS (CONFIG_FILE_SYSTEM).
 */
#include <pymergetic/metal/sys/hostinfo.h>

#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static int mkdir_p(const char *path)
{
	char buf[64];
	size_t len = strlen(path);

	if (len == 0 || len >= sizeof(buf)) {
		return -1;
	}

	memcpy(buf, path, len + 1);

	for (char *p = buf + 1; *p != '\0'; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (fs_mkdir(buf) != 0) {
			/* ignore EEXIST */
		}
		*p = '/';
	}

	if (fs_mkdir(buf) != 0) {
		/* ignore EEXIST */
	}

	return 0;
}

int pm_metal_sys_hostinfo_publish(const char *vfs_root, const void *blob, size_t blob_len)
{
	char path[80];
	struct fs_file_t file;
	ssize_t wrote;
	int rc;

	if (vfs_root == NULL || blob == NULL || blob_len == 0) {
		return -1;
	}

	if (snprintf(path, sizeof(path), "%s/bootstrap", vfs_root) >= (int)sizeof(path)) {
		return -1;
	}

	mkdir_p(vfs_root);

	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
	if (rc != 0) {
		printk("pm_hostinfo: fs_open %s failed: %d\n", path, rc);
		return -1;
	}

	wrote = fs_write(&file, blob, blob_len);
	rc = fs_close(&file);
	if (wrote != (ssize_t)blob_len || rc != 0) {
		printk("pm_hostinfo: fs_write %s failed: wrote=%zd rc=%d\n", path, wrote, rc);
		return -1;
	}

	printk("pm_hostinfo: published bootstrap (%zu bytes) -> %s\n", blob_len, path);
	return 0;
}
