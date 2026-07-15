/*
 * tmpfs — impl: linux. See mount/tmpfs.h.
 *
 * Delegates to the host's own real tmpfs: mkdtemp() a fresh directory
 * under /dev/shm, then treat it internally exactly like an ordinary
 * hostdir — no device layer, no on-disk format, just a real, already-
 * mounted RAM-backed filesystem WAMR's existing POSIX WASI backend
 * already knows how to openat() into. See docs/MOUNT.md "fstype vs.
 * source" / "Named ramdisks".
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* mkdtemp(), nftw() — see man 7 feature_test_macros */
#endif

#include "pymergetic/metal/mount/tmpfs.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pymergetic/metal/mount/tmpfs_registry.h"

#define PM_METAL_MOUNT_TMPFS_LINUX_TEMPLATE "/dev/shm/pm_metal_tmpfs_XXXXXX"

/* nftw() callback — removes both plain files and (already-emptied, thanks
 * to FTW_DEPTH below) directories. */
static int pm_metal_mount_tmpfs_nftw_remove(const char *path, const struct stat *sb, int typeflag,
					     struct FTW *ftwbuf)
{
	(void)sb;
	(void)ftwbuf;
	if (typeflag == FTW_DP) {
		return rmdir(path);
	}
	return unlink(path);
}

/* Recursive "rm -rf path" — only ever called on a directory this file
 * itself mkdtemp()'d, never on caller-supplied input. FTW_DEPTH: visit
 * children before the directory containing them, so each rmdir() above
 * only ever runs on an already-emptied directory. FTW_PHYS: never follow
 * symlinks (nothing under our own mkdtemp()'d tree should ever be one —
 * safe default regardless). */
static void pm_metal_mount_tmpfs_rm_rf(const char *path)
{
	if (nftw(path, pm_metal_mount_tmpfs_nftw_remove, 16, FTW_DEPTH | FTW_PHYS) != 0) {
		fprintf(stderr, "pm_metal_mount: tmpfs: cleanup failed for %s\n", path);
	}
}

static int pm_metal_mount_tmpfs_establish(const char *source, const char *opts, char *out_host_path,
					   size_t out_cap)
{
	char template[] = PM_METAL_MOUNT_TMPFS_LINUX_TEMPLATE;
	char *created;
	size_t len;

	if (!source || !source[0] || !out_host_path) {
		return -1;
	}

	if (pm_metal_mount_tmpfs_registry_acquire(source, out_host_path, out_cap) == 0) {
		/* Reuse — see docs/MOUNT.md "Named ramdisks": first
		 * establish() for a name wins, later references reuse it
		 * and ignore any conflicting opts on that later line. */
		if (opts && opts[0]) {
			fprintf(stderr,
				"pm_metal_mount: tmpfs: '%s' already established, ignoring options on this line\n",
				source);
		}
		return 0;
	}

	created = mkdtemp(template);
	if (!created) {
		fprintf(stderr, "pm_metal_mount: tmpfs: mkdtemp under /dev/shm failed for '%s'\n", source);
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
		pm_metal_mount_tmpfs_rm_rf(out_host_path);
		return -1;
	}

	/* `size=` (if present) is accepted but not enforced here: /dev/shm
	 * is already one real, host-wide-capped tmpfs — unlike zephyr,
	 * where a fstab `size=` is validated against a hard, board-fixed
	 * ramdisk capacity, linux genuinely can grow up to whatever the
	 * host's own tmpfs allows. See docs/MOUNT.md "Named ramdisks". */
	(void)opts;

	return 0;
}

static void pm_metal_mount_tmpfs_release(const char *host_path)
{
	int rc = pm_metal_mount_tmpfs_registry_release(host_path);

	if (rc == 1) {
		pm_metal_mount_tmpfs_rm_rf(host_path);
	} else if (rc < 0) {
		fprintf(stderr, "pm_metal_mount: tmpfs: release() of untracked path %s\n", host_path);
	}
	/* rc == 0: other mount-table entries still reference this same
	 * named tmpfs — leave the backing directory alone. */
}

static const pm_metal_mount_ops_t g_pm_metal_mount_tmpfs_ops = {
	.establish = pm_metal_mount_tmpfs_establish,
	.release = pm_metal_mount_tmpfs_release,
};

const pm_metal_mount_ops_t *pm_metal_mount_tmpfs_ops(void)
{
	return &g_pm_metal_mount_tmpfs_ops;
}
