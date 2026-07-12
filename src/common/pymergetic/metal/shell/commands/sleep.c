/*
 * Shell builtin — `sleep`. See sleep.h.
 */
#include "pymergetic/metal/shell/commands/sleep.h"

#include <stdint.h>
#include <stdlib.h>

#include "pymergetic/metal/port/intr.h"
#include "pymergetic/metal/port/sleep.h"
#include "pymergetic/metal/util/log.h"

/* Chunk size for the poll loop below — small enough that Ctrl+C feels
 * immediate, large enough not to spin. */
#define PM_METAL_SHELL_SLEEP_CHUNK_MS 100U

int pm_metal_shell_cmd_sleep(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	if (argc < 2) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "usage: sleep <seconds>");
		return -1;
	}

	char *end;
	double secs = strtod(argv[1], &end);

	if (end == argv[1] || secs < 0) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "sleep: invalid duration: %s", argv[1]);
		return -1;
	}

	/* Blocks whichever console's own dispatcher thread called this
	 * (see app.c's kernel_dispatch_thread — the kernel console's is
	 * the only one that exists today) — any command typed on the same
	 * console while this is in flight just sits buffered until it
	 * returns, same as a real shell's own `sleep`.
	 *
	 * Slept in small chunks, polling pm_metal_port_intr_requested()
	 * between them, rather than one big pm_metal_port_sleep_ms() call
	 * — a process-directed SIGINT is not guaranteed to land on *this*
	 * thread (POSIX may deliver it to any thread in the process that
	 * doesn't have it blocked, and in practice it often lands on
	 * app.c's own pump-loop thread instead, not this dispatcher
	 * thread), so a single blocking sleep() call here could sit past
	 * Ctrl+C entirely, and then block app.c's shutdown join() on this
	 * thread for the rest of the requested duration. Polling the same
	 * flag pm_metal_port_intr_requested() exposes (set once, from
	 * whichever thread the signal actually hit) sidesteps that: any
	 * thread can observe it, regardless of which one the OS delivered
	 * the signal to. */
	uint32_t remaining_ms = (uint32_t)(secs * 1000.0 + 0.5);

	while (remaining_ms > 0 && !pm_metal_port_intr_requested()) {
		uint32_t chunk = remaining_ms < PM_METAL_SHELL_SLEEP_CHUNK_MS ? remaining_ms
									      : PM_METAL_SHELL_SLEEP_CHUNK_MS;

		pm_metal_port_sleep_ms(chunk);
		remaining_ms -= chunk;
	}
	return 0;
}
