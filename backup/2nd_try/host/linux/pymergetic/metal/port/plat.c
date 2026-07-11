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

#define PM_METAL_WAMR_POOL_MIN_BYTES 65536U

static uint8_t *g_wamr_pool;
static size_t g_wamr_pool_size;

int pm_metal_port_wamr_pool_establish(uint8_t **out_buf, size_t *out_size)
{
	uint64_t budget;
	size_t pool_size;

	if (out_buf == NULL || out_size == NULL) {
		return -1;
	}

	if (g_wamr_pool != NULL) {
		*out_buf = g_wamr_pool;
		*out_size = g_wamr_pool_size;
		return 0;
	}

	budget = pm_metal_port_arena_budget();
	if (budget < PM_METAL_WAMR_POOL_MIN_BYTES) {
		return -1;
	}

	pool_size = (size_t)((budget > (uint64_t)SIZE_MAX) ? SIZE_MAX : budget);
	g_wamr_pool = malloc(pool_size);
	if (g_wamr_pool == NULL) {
		return -1;
	}

	g_wamr_pool_size = pool_size;
	*out_buf = g_wamr_pool;
	*out_size = g_wamr_pool_size;
	return 0;
}
