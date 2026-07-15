/*
 * Port — linux bind implementation. See pipe.h.
 */
#include "pymergetic/metal/port/pipe.h"

#include <unistd.h>

int pm_metal_port_pipe(int64_t *out_read_fd, int64_t *out_write_fd)
{
	int fds[2];

	if (!out_read_fd || !out_write_fd) {
		return -1;
	}
	if (pipe(fds) != 0) {
		return -1;
	}
	*out_read_fd = fds[0];
	*out_write_fd = fds[1];
	return 0;
}

void pm_metal_port_close(int64_t fd)
{
	if (fd >= 0) {
		close((int)fd);
	}
}
