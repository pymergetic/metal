/*
 * Port — zephyr bind. Stub — deferred (see docs/RUNTIME.md "Bring-up
 * plan" §5). A real implementation needs an actual in-kernel pipe
 * primitive (zephyr has no POSIX pipe(2); a k_pipe or a small ring
 * buffer + poll signal would stand in for one) wired to fd numbers
 * WASI's own os_convert_std*_handle() will accept — nontrivial enough
 * that it's left unattempted rather than half-done.
 */
#include "pymergetic/metal/port/pipe.h"

int pm_metal_port_pipe(int64_t *out_read_fd, int64_t *out_write_fd)
{
	(void)out_read_fd;
	(void)out_write_fd;
	return -1;
}

void pm_metal_port_close(int64_t fd)
{
	(void)fd;
}
