/*
 * Port — linux bind implementations.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pymergetic/metal/port/platform.h"

/*
 * Real probe (total installed host RAM) — diagnostics only. Unlike zephyr,
 * where this drives the WAMR arena budget (the firmware owns the whole
 * machine), linux WAMR pool sizing stays sourced from the explicit
 * memory_bytes given to pm_metal_runtime_init(): the process shares the host
 * with everything else, so "total machine RAM" is not "RAM this process may
 * claim". See docs/RUNTIME.md.
 */
uint64_t pm_metal_port_machine_ram(void)
{
	long pages = sysconf(_SC_PHYS_PAGES);
	long page_size = sysconf(_SC_PAGE_SIZE);

	if (pages < 0 || page_size < 0) {
		return 0;
	}

	return (uint64_t)pages * (uint64_t)page_size;
}

static void *g_pm_metal_port_wamr_pool;
static uint64_t g_pm_metal_port_wamr_pool_bytes;

void *pm_metal_port_wamr_pool_establish(uint64_t requested_bytes, uint64_t *out_bytes)
{
	void *pool = malloc((size_t)requested_bytes);

	if (!pool) {
		return NULL;
	}

	g_pm_metal_port_wamr_pool = pool;
	g_pm_metal_port_wamr_pool_bytes = requested_bytes;
	*out_bytes = requested_bytes;

	return pool;
}

void pm_metal_port_wamr_pool_release(void)
{
	free(g_pm_metal_port_wamr_pool);
	g_pm_metal_port_wamr_pool = NULL;
	g_pm_metal_port_wamr_pool_bytes = 0;
}

uint64_t pm_metal_port_wamr_pool_bytes(void)
{
	return g_pm_metal_port_wamr_pool_bytes;
}

int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len)
{
	FILE *f = fopen(host_path, "rb");
	if (!f) {
		return -1;
	}

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	long size = ftell(f);
	if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}

	uint8_t *buf = malloc((size_t)size);
	if (!buf) {
		fclose(f);
		return -1;
	}

	size_t n = fread(buf, 1, (size_t)size, f);
	fclose(f);
	if (n != (size_t)size) {
		free(buf);
		return -1;
	}

	*out_buf = buf;
	*out_len = (uint32_t)size;
	return 0;
}
