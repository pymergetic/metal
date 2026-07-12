/*
 * Port — linux bind implementation. See intr.h.
 *
 * The handler below does exactly one thing — set a sig_atomic_t — and
 * nothing else: POSIX only guarantees a small set of operations are
 * safe to perform inside a signal handler (see signal-safety(7)), and
 * "call into this codebase's own logging/console/runtime code" is not
 * one of them. requested() is polled from ordinary (non-handler)
 * context instead, exactly like app.c already polls its own
 * quit-requested flag — this file adds no new concurrency model, just
 * one more thing feeding the same kind of flag.
 */
#include "pymergetic/metal/port/intr.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t g_pm_metal_port_intr_flag;

static void pm_metal_port_intr_handler(int sig)
{
	(void)sig;
	g_pm_metal_port_intr_flag = 1;
}

void pm_metal_port_intr_install(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = pm_metal_port_intr_handler;
	sigaction(SIGINT, &sa, NULL);
}

int pm_metal_port_intr_requested(void)
{
	return g_pm_metal_port_intr_flag != 0;
}
