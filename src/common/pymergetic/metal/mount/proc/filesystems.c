#include "pymergetic/metal/mount/proc/filesystems.h"

#include <stdio.h>

#include "pymergetic/metal/mount/ops.h"

int pm_metal_mount_proc_generate_filesystems(char *out, size_t cap, size_t *out_len)
{
	size_t used = 0;
	int i;

	if (!out || !out_len || cap == 0) {
		return -1;
	}
	out[0] = '\0';
	for (i = 0; i < (int)PM_METAL_MOUNT_KIND_COUNT; i++) {
		const char *name = pm_metal_mount_kind_name((pm_metal_mount_kind_t)i);
		int n;

		if (!name) {
			continue;
		}
		n = snprintf(out + used, cap > used ? cap - used : 0, "nodev\t%s\n", name);
		if (n < 0 || (size_t)n >= (cap > used ? cap - used : 0)) {
			return -1;
		}
		used += (size_t)n;
	}
	*out_len = used;
	return 0;
}
