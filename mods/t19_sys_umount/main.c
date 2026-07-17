/*
 * T19 — privileged umount("/dyn") after t18 used it. Needs MOUNT marker.
 * Privileged: define matches build-mod -DPM_METAL_BUILD_KERNEL.
 */
#ifndef PM_METAL_BUILD_KERNEL
#define PM_METAL_BUILD_KERNEL 1
#endif

#include <stdio.h>

#include "pymergetic/metal/mount/mount.h"

int main(void)
{
	if (umount("/dyn") != 0) {
		printf("t19_sys_umount: umount failed\n");
		return 1;
	}
	printf("t19_sys_umount: umounted /dyn\n");
	return 0;
}
