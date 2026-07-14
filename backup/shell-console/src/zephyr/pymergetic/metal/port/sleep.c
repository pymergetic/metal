/*
 * Port — zephyr bind. Stub — deferred with the rest of zephyr's console/
 * shell (see docs/RUNTIME.md "Bring-up plan" §5). A real implementation
 * is just k_sleep(K_MSEC(ms)) — trivial once the rest of zephyr's shell
 * exists to call it from.
 */
#include "pymergetic/metal/port/sleep.h"

void pm_metal_port_sleep_ms(uint32_t ms)
{
	(void)ms;
}
