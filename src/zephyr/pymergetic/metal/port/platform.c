/*
 * Port — zephyr bind implementations. Memory ops live in
 * src/zephyr/pymergetic/metal/memory/ instead — see platform.h.
 */
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "pymergetic/metal/port/platform.h"

/* stub — fs_open()/fs_read() backend pending, see docs/RUNTIME.md §5 */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len)
{
	(void)host_path;
	(void)out_buf;
	(void)out_len;
	return -1;
}

/* stub — fs_stat() backend pending, see docs/RUNTIME.md §5. Returning 0
 * (never "exists") is the safe default while zephyr's filesystem
 * support stays deferred. */
int pm_metal_port_file_exists(const char *host_path)
{
	(void)host_path;
	return 0;
}

int pm_metal_port_write_file(const char *host_path, const uint8_t *data, uint32_t len)
{
	(void)host_path;
	(void)data;
	(void)len;
	return -1;
}

int pm_metal_port_mkdir(const char *host_path)
{
	(void)host_path;
	return -1;
}

uint64_t pm_metal_port_monotonic_ms(void)
{
	return (uint64_t)k_uptime_get();
}
