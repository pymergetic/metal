/*
 * T29 — guest mount() must fail when the runtime did not grant
 * --allow-guest-mount. Exit 0 on denial; exit 1 if mount unexpectedly works.
 * Privileged: empty MOUNT marker; define matches build-mod -DPM_METAL_BUILD_KERNEL.
 */
#ifndef PM_METAL_BUILD_KERNEL
#define PM_METAL_BUILD_KERNEL 1
#endif

#include <stdio.h>

#include "pymergetic/metal/mount/mount.h"

int main(void)
{
	if (pm_metal_mount_mount("dyn", "/dyn", "tmpfs", NULL) != 0) {
		printf("t29_sys_mount_denied: mount rejected (expected)\n");
		return 0;
	}
	printf("t29_sys_mount_denied: mount unexpectedly succeeded\n");
	return 1;
}
