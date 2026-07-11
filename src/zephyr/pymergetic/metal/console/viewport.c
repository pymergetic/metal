/*
 * Console — zephyr bind. Only pump() is stubbed here now —
 * init()/shutdown()/register()/unregister()/focus()/set_filter() moved to
 * src/common/pymergetic/metal/console/viewport.c (impl: common; none of
 * that logic was actually OS-specific, see docs/CONSOLE.md "What's
 * common vs bind") and already work on every target, zephyr included.
 * pump() itself is still deferred with the rest of zephyr (see
 * docs/RUNTIME.md "Bring-up plan" §5): LOCAL's real implementation needs
 * a real terminal to multiplex onto (UART shell, later SDL) and
 * console.c's k_pipe-based sinks (see there) before it can do anything;
 * NETWORK stays a stub on every target for now (see docs/CONSOLE.md "Not
 * yet").
 */
#include "pymergetic/metal/console/viewport.h"

int pm_metal_viewport_pump(pm_metal_viewport_kind_t kind)
{
	(void)kind;
	return -1;
}
