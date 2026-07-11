/*
 * Port — zephyr bind implementations. Memory ops live in
 * src/zephyr/pymergetic/metal/memory/ instead — see platform.h.
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/port/platform.h"

/* stub — fs_open()/fs_read() backend pending, see docs/RUNTIME.md §5 */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len)
{
	(void)host_path;
	(void)out_buf;
	(void)out_len;
	return -1;
}
