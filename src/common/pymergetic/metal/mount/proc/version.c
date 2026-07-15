#include "pymergetic/metal/mount/proc/version.h"

#include <stdio.h>

#include "pymergetic/metal/mount/proc/util.h"
#include "pymergetic/metal/port/platform.h"

#define PM_METAL_MOUNT_PROC_VERSION "0.1.0"

int pm_metal_mount_proc_generate_version(char *out, size_t cap, size_t *out_len)
{
	char buf[128];
	int n;

	n = snprintf(buf, sizeof(buf), "pymergetic-metal %s (%s wasm32-wasi)\n", PM_METAL_MOUNT_PROC_VERSION,
		     pm_metal_port_target_name(pm_metal_port_target_id()));
	if (n < 0 || (size_t)n >= sizeof(buf)) {
		return -1;
	}
	return pm_metal_mount_proc_put_str(out, cap, out_len, buf);
}
