/*
 * Port — zephyr bind. Stub — deferred with the rest of zephyr's console/
 * shell (see docs/RUNTIME.md "Bring-up plan" §5). A real implementation
 * would go through Zephyr's shell backend or a UART driver, whichever
 * the LOCAL viewport's real bind ends up using.
 */
#include "pymergetic/metal/port/term.h"

void pm_metal_port_term_write(const void *buf, size_t len)
{
	(void)buf;
	(void)len;
}
