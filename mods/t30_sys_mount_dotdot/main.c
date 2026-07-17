/*
 * T30 — guest mount() target with ".." must be rejected (zip-slip /
 * path-normalization). Exit 0 on rejection; exit 1 if mount works.
 * Privileged: empty MOUNT marker; define matches build-mod -DPM_METAL_BUILD_KERNEL.
 */
#ifndef PM_METAL_BUILD_KERNEL
#define PM_METAL_BUILD_KERNEL 1
#endif

#include <stdio.h>

#include "pymergetic/metal/mount/mount.h"

int main(void)
{
	if (pm_metal_mount_mount("dyn", "/tmp/../dyn", "tmpfs", NULL) != 0) {
		printf("t30_sys_mount_dotdot: mount rejected (expected)\n");
		return 0;
	}
	printf("t30_sys_mount_dotdot: mount with .. unexpectedly succeeded\n");
	return 1;
}
