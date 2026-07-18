/*
 * T22 — fstype iterator (pm_metal_mount_fstype_count/name). Public readonly
 * API — no MOUNT marker / KERNEL required (same info as /proc/filesystems).
 */
#include <stdio.h>

#include "pymergetic/metal/mount/mount.h"

int main(void)
{
	int n = pm_metal_mount_fstype_count();
	int i;
	char name[32];

	if (n <= 0) {
		printf("t22_fstypes: count failed\n");
		return 1;
	}
	for (i = 0; i < n; i++) {
		if (pm_metal_mount_fstype_name((unsigned)i, name, sizeof(name)) != 0) {
			printf("t22_fstypes: name(%d) failed\n", i);
			return 1;
		}
		printf("%s\n", name);
	}
	return 0;
}
