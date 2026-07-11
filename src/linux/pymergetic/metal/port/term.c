/*
 * Port — linux bind implementation. Real terminal output == real stdout.
 */
#include "pymergetic/metal/port/term.h"

#include <unistd.h>

void pm_metal_port_term_write(const void *buf, size_t len)
{
	ssize_t written = write(STDOUT_FILENO, buf, len);

	(void)written; /* best-effort — see term.h */
}
