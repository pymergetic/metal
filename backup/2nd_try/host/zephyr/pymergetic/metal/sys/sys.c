#include <pymergetic/metal/sys/sys.h>
#include <pymergetic/metal/port/plat.h>

#include <string.h>
#include <time.h>

static uint64_t pm_metal_sys_built_stamp(void)
{
	time_t now = time(NULL);

	if (now > 0) {
		return (uint64_t)now;
	}
#ifdef PM_METAL_BUILT_EPOCH
	return (uint64_t)PM_METAL_BUILT_EPOCH;
#else
	return 0;
#endif
}

int pm_metal_sys_bootstrap_encode(pm_metal_sys_bootstrap_t *out)
{
	if (out == NULL) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	pm_metal_util_bigtag_set(&out->tag, PM_METAL_SYS_MAGIC, PM_METAL_SYS_VERSION,
				 pm_metal_sys_built_stamp());
	out->size = PM_METAL_SYS_BLOB_SIZE;
	out->machine_ram = pm_metal_port_machine_ram();
	out->link_used = pm_metal_port_link_used();
	out->arena_budget = pm_metal_port_arena_budget();
	return 0;
}
