#include <pymergetic/metal/sys/sys.h>
#include <pymergetic/metal/port/plat.h>

#include <string.h>
#include <time.h>

int pm_metal_sys_bootstrap_encode(pm_metal_sys_bootstrap_t *out)
{
	if (out == NULL) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	pm_metal_util_bigtag_set(&out->tag, PM_METAL_SYS_MAGIC, PM_METAL_SYS_VERSION,
				 (uint64_t)time(NULL));
	out->size = PM_METAL_SYS_BLOB_SIZE;
	out->machine_ram = pm_metal_port_machine_ram();
	out->link_used = pm_metal_port_link_used();
	out->arena_budget = pm_metal_port_arena_budget();
	return 0;
}
