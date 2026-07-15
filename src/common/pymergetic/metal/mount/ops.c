/*
 * Mount — resolve()/by-name dispatch (impl: common). Just forwards to the
 * dedicated getter matching kind — see ops.h, mirrors memory/ops.c.
 */
#include "pymergetic/metal/mount/ops.h"

#include <string.h>

#include "pymergetic/metal/mount/hostdir.h"
#include "pymergetic/metal/mount/tmpfs.h"

const pm_metal_mount_ops_t *pm_metal_mount_resolve_kind(pm_metal_mount_kind_t kind)
{
	switch (kind) {
	case PM_METAL_MOUNT_HOSTDIR:
		return pm_metal_mount_hostdir_ops();
	case PM_METAL_MOUNT_TMPFS:
		return pm_metal_mount_tmpfs_ops();
	default:
		return NULL;
	}
}

int pm_metal_mount_kind_by_name(const char *name, pm_metal_mount_kind_t *out_kind)
{
	if (!name || !out_kind) {
		return -1;
	}
	if (strcmp(name, "hostdir") == 0) {
		*out_kind = PM_METAL_MOUNT_HOSTDIR;
		return 0;
	}
	if (strcmp(name, "tmpfs") == 0) {
		*out_kind = PM_METAL_MOUNT_TMPFS;
		return 0;
	}
	return -1;
}
