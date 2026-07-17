/*
 * tmpfs — impl: nuttx. See mount/tmpfs.h.
 *
 * Same shape as linux: mkdtemp() a fresh directory, then treat it as an
 * ordinary hostdir for WAMR's POSIX WASI backend. NuttX sim typically has
 * /tmp (not /dev/shm), and may lack nftw() — so teardown is a small
 * recursive unlink/rmdir walk instead.
 */
#include "pymergetic/metal/mount/tmpfs.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pymergetic/metal/mount/tmpfs_registry.h"

#define PM_METAL_MOUNT_TMPFS_NUTTX_TEMPLATE "/tmp/pm_metal_tmpfs_XXXXXX"

static int pm_metal_mount_tmpfs_rm_rf(const char *path);

static int pm_metal_mount_tmpfs_rm_rf_dir(const char *path)
{
	DIR *d = opendir(path);
	struct dirent *ent;
	char child[PATH_MAX];
	int rc = 0;

	if (!d) {
		return -1;
	}
	while ((ent = readdir(d)) != NULL) {
		size_t n;

		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}
		n = (size_t)snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
		if (n >= sizeof(child)) {
			rc = -1;
			break;
		}
		if (pm_metal_mount_tmpfs_rm_rf(child) != 0) {
			rc = -1;
			break;
		}
	}
	closedir(d);
	if (rc != 0) {
		return rc;
	}
	return rmdir(path);
}

static int pm_metal_mount_tmpfs_rm_rf(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0) {
		return -1;
	}
	if (S_ISDIR(st.st_mode)) {
		return pm_metal_mount_tmpfs_rm_rf_dir(path);
	}
	return unlink(path);
}

static void pm_metal_mount_tmpfs_cleanup(const char *path)
{
	if (pm_metal_mount_tmpfs_rm_rf(path) != 0) {
		fprintf(stderr, "pm_metal_mount: tmpfs: cleanup failed for %s\n", path);
	}
}

static int pm_metal_mount_tmpfs_establish(const char *source, const char *opts, char *out_host_path,
					   size_t out_cap)
{
	char template[] = PM_METAL_MOUNT_TMPFS_NUTTX_TEMPLATE;
	char *created;
	size_t len;

	if (!source || !source[0] || !out_host_path) {
		return -1;
	}

	if (pm_metal_mount_tmpfs_registry_acquire(source, out_host_path, out_cap) == 0) {
		if (opts && opts[0]) {
			fprintf(stderr,
				"pm_metal_mount: tmpfs: '%s' already established, ignoring options on this line\n",
				source);
		}
		return 0;
	}

	created = mkdtemp(template);
	if (!created) {
		fprintf(stderr, "pm_metal_mount: tmpfs: mkdtemp under /tmp failed for '%s'\n", source);
		return -1;
	}

	len = strlen(created);
	if (len + 1 > out_cap) {
		fprintf(stderr, "pm_metal_mount: tmpfs: host path too long for '%s'\n", source);
		rmdir(created);
		return -1;
	}
	memcpy(out_host_path, created, len + 1);

	if (pm_metal_mount_tmpfs_registry_insert(source, out_host_path) != 0) {
		fprintf(stderr, "pm_metal_mount: tmpfs: registry full, dropping '%s'\n", source);
		pm_metal_mount_tmpfs_cleanup(out_host_path);
		return -1;
	}

	(void)opts;
	return 0;
}

static void pm_metal_mount_tmpfs_release(const char *host_path)
{
	int rc = pm_metal_mount_tmpfs_registry_release(host_path);

	if (rc == 1) {
		pm_metal_mount_tmpfs_cleanup(host_path);
	} else if (rc < 0) {
		fprintf(stderr, "pm_metal_mount: tmpfs: release() of untracked path %s\n", host_path);
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
