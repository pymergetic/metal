#include <pymergetic/metal/sys/sys.h>
#include <pymergetic/metal/port/plat.h>

#include <string.h>

int pm_metal_sys_bootstrap_validate(const pm_metal_sys_bootstrap_t *blob)
{
	if (blob == NULL) {
		return -1;
	}
	if (!pm_metal_util_wiretag_valid(&blob->tag, PM_METAL_SYS_MAGIC, PM_METAL_SYS_VERSION)) {
		return -1;
	}
	if (blob->size != PM_METAL_SYS_BLOB_SIZE) {
		return -1;
	}
	return 0;
}

int pm_metal_sys_bootstrap_encode(pm_metal_sys_bootstrap_t *out)
{
	if (out == NULL) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	pm_metal_util_wiretag_set(&out->tag, PM_METAL_SYS_MAGIC, PM_METAL_SYS_VERSION);
	out->size = PM_METAL_SYS_BLOB_SIZE;
	out->machine_ram = pm_metal_port_machine_ram();
	out->link_used = pm_metal_port_link_used();
	out->arena_budget = pm_metal_port_arena_budget();
	return 0;
}
