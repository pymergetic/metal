#include "pymergetic/metal/mount/proc/cpuinfo.h"

#include <stdio.h>

#include "pymergetic/metal/mount/proc/util.h"
#include "pymergetic/metal/port/platform.h"

int pm_metal_mount_proc_generate_cpuinfo(char *out, size_t cap, size_t *out_len)
{
	char buf[256];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "processor\t: 0\n"
		     "model name\t: pymergetic-metal wasm32\n"
		     "features\t: wasi\n"
		     "platform\t: %s\n",
		     pm_metal_port_target_name(pm_metal_port_target_id()));
	if (n < 0 || (size_t)n >= sizeof(buf)) {
		return -1;
	}
	return pm_metal_mount_proc_put_str(out, cap, out_len, buf);
}
