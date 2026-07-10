#include <pymergetic/metal/port/plat.h>

#include <stdlib.h>
#include <string.h>

static uint64_t parse_u64_env(const char *name, uint64_t fallback)
{
	const char *v = getenv(name);

	if (v == NULL || v[0] == '\0') {
		return fallback;
	}

	return (uint64_t)strtoull(v, NULL, 0);
}

pm_metal_port_target_id_t pm_metal_port_target_id(void)
{
	return PM_METAL_PORT_TARGET_LINUX;
}

uint64_t pm_metal_port_machine_ram(void)
{
	return parse_u64_env("PM_MACHINE_RAM", 512ULL * 1024ULL * 1024ULL);
}

uint64_t pm_metal_port_link_used(void)
{
	return parse_u64_env("PM_LINK_USED", 64ULL * 1024ULL * 1024ULL);
}

uint64_t pm_metal_port_arena_budget(void)
{
	uint64_t machine = pm_metal_port_machine_ram();
	uint64_t link = pm_metal_port_link_used();

	if (machine <= link) {
		return 0;
	}
	return machine - link;
}
