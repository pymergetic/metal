/*
 * Port — linux bind implementation. See sleep.h.
 */
#include "pymergetic/metal/port/sleep.h"

#include <time.h>

void pm_metal_port_sleep_ms(uint32_t ms)
{
	struct timespec ts;

	ts.tv_sec = (time_t)(ms / 1000);
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL); /* best-effort — see sleep.h on early return */
}
