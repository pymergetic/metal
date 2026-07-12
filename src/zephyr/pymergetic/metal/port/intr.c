/*
 * Port — zephyr bind. Stub — deferred with the rest of zephyr's console/
 * shell (see docs/RUNTIME.md "Bring-up plan" §5). Zephyr has no SIGINT/
 * real terminal in the posix sense; a real implementation here would
 * hook whatever the LOCAL viewport's real bind ends up using as "the
 * operator wants out" (e.g. a dedicated key combo through the shell
 * backend), same deferral as port/term.h and port/dir.h.
 */
#include "pymergetic/metal/port/intr.h"

void pm_metal_port_intr_install(void)
{
}

int pm_metal_port_intr_requested(void)
{
	return 0;
}
