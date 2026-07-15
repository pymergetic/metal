/*
 * hostdir — impl: common (see hostdir.h for why one impl covers every
 * target).
 */
#include "pymergetic/metal/mount/hostdir.h"

#include <string.h>

static int pm_metal_mount_hostdir_establish(const char *source, const char *opts, char *out_host_path,
					     size_t out_cap)
{
	size_t len;

	(void)opts; /* hostdir has no kind-specific options */

	if (!source || !source[0] || !out_host_path) {
		return -1;
	}
	len = strlen(source);
	if (len + 1 > out_cap) {
		return -1;
	}
	memcpy(out_host_path, source, len + 1);
	return 0;
}

static void pm_metal_mount_hostdir_release(const char *host_path)
{
	/* Never ours to remove — the caller owns this directory's lifetime,
	 * same as today's single vfs_root. */
	(void)host_path;
}

static const pm_metal_mount_ops_t g_pm_metal_mount_hostdir_ops = {
	.establish = pm_metal_mount_hostdir_establish,
	.release = pm_metal_mount_hostdir_release,
};

const pm_metal_mount_ops_t *pm_metal_mount_hostdir_ops(void)
{
	return &g_pm_metal_mount_hostdir_ops;
}
