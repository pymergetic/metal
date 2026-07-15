#include "pymergetic/metal/mount/proc/uptime.h"

#include <stdint.h>
#include <stdio.h>

#include "pymergetic/metal/mount/proc/boot.h"
#include "pymergetic/metal/mount/proc/util.h"
#include "pymergetic/metal/port/platform.h"

int pm_metal_mount_proc_generate_uptime(char *out, size_t cap, size_t *out_len)
{
	uint64_t now = pm_metal_port_monotonic_ms();
	uint64_t elapsed_ms;
	uint64_t sec;
	uint64_t centi;
	char buf[64];
	int n;

	if (!pm_metal_mount_proc_boot_is_set() || now < pm_metal_mount_proc_boot_ms()) {
		elapsed_ms = 0;
	} else {
		elapsed_ms = now - pm_metal_mount_proc_boot_ms();
	}
	sec = elapsed_ms / 1000ull;
	centi = (elapsed_ms % 1000ull) / 10ull;
	/* second field unused (idle) — keep Linux shape */
	n = snprintf(buf, sizeof(buf), "%llu.%02llu 0.00\n", (unsigned long long)sec,
		     (unsigned long long)centi);
	if (n < 0 || (size_t)n >= sizeof(buf)) {
		return -1;
	}
	return pm_metal_mount_proc_put_str(out, cap, out_len, buf);
}
