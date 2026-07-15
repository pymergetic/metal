/*
 * T19 — privileged umount("/dyn") after t18 used it. Needs MOUNT marker.
 */
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
