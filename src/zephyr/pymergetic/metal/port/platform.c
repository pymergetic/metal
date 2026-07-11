/*
 * Port — zephyr bind implementations.
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/port/platform.h"

uint64_t pm_metal_port_machine_ram(void)
{
	return 0;
}

/* stub — probe + arena_budget math pending, see docs/RUNTIME.md §5 */
void *pm_metal_port_wamr_pool_establish(uint64_t requested_bytes, uint64_t *out_bytes)
{
	(void)requested_bytes;
	(void)out_bytes;
	return NULL;
}

void pm_metal_port_wamr_pool_release(void)
{
}

uint64_t pm_metal_port_wamr_pool_bytes(void)
{
	return 0;
}

/* stub — fs_open()/fs_read() backend pending, see docs/RUNTIME.md §5 */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len)
{
	(void)host_path;
	(void)out_buf;
	(void)out_len;
	return -1;
}
