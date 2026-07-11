/*
 * Console — sinks. Host-only (never built into a mod). See docs/CONSOLE.md
 * for the full picture; short version here.
 *
 * A "sink" is one full-duplex byte-pipe pair between a producer and a
 * consumer of a single console's worth of bytes:
 *
 *   PM_METAL_CONSOLE_KERNEL — the runtime process's own diagnostics.
 *     producer: the host itself, via sink->out (fprintf/pm_metal_util_log_write).
 *     consumer: the kernel command dispatcher, via sink->in (fgets).
 *
 *   PM_METAL_CONSOLE_HANDLE — one loaded module's WASI stdio.
 *     producer: the guest's WASI fd 1/2, via sink->producer_out_fd handed
 *     to pm_metal_runtime_run_ex() as stdout_fd/stderr_fd.
 *     consumer: the guest's WASI fd 0, via sink->consumer_in_fd handed to
 *     run_ex() as stdin_fd.
 *
 * Both kinds are the *same* struct and the *same* open()/close() — kernel
 * is a peer sink, not a special case (see docs/CONSOLE.md "Kernel is a
 * peer, not special"). console.c only allocates/frees the two pipes; it
 * never calls into runtime.h itself and knows nothing about focus,
 * rendering, or multiplexing — that's console/viewport.h, one layer up.
 */
#ifndef PYMERGETIC_METAL_CONSOLE_CONSOLE_H_
#define PYMERGETIC_METAL_CONSOLE_CONSOLE_H_

#include <stdio.h>

#include "pymergetic/metal/runtime/runtime.h"

#define PM_METAL_CONSOLE_LABEL_MAX 24

typedef enum pm_metal_console_kind {
	PM_METAL_CONSOLE_KERNEL = 0,
	PM_METAL_CONSOLE_HANDLE,
} pm_metal_console_kind_t;

/* Plain data, not opaque — viewport.c (the only other reader) needs the
 * raw fds for poll(). Nothing enforces "read-only outside console.c"
 * beyond convention, same as every other struct in this codebase. */
typedef struct pm_metal_console_sink {
	pm_metal_console_kind_t kind;
	pm_metal_runtime_handle_t handle; /* meaningful for PM_METAL_CONSOLE_HANDLE only */
	char label[PM_METAL_CONSOLE_LABEL_MAX]; /* "kernel" / "handle-<N>" — viewport's focus list */

	FILE *out; /* producer writes here */
	FILE *in;  /* consumer reads here */

	int producer_out_fd;   /* == fileno(out); HANDLE kind: run_ex()'s stdout_fd/stderr_fd */
	int consumer_in_fd;    /* == fileno(in);  HANDLE kind: run_ex()'s stdin_fd */
	int viewport_drain_fd; /* viewport polls + reads this for output */
	int viewport_feed_fd;  /* viewport writes operator/network input here */
} pm_metal_console_sink_t;

/* impl: bind — src/linux/pymergetic/metal/console/console.c
 *              src/zephyr/pymergetic/metal/console/console.c (stub — deferred with the rest of zephyr, see docs/RUNTIME.md "Bring-up plan")
 *
 * open(): allocates two pipes (4 fds total, split across the fields
 * above) and fills in *out. `handle`/`label` are stored verbatim; open()
 * never calls into runtime.h — the caller still owns deciding *when* a
 * handle's module runs and passing this sink's fds to run_ex() itself.
 * close(): closes all 4 fds and the two FILE* wrappers. Returns 0/-1. */
int pm_metal_console_open(pm_metal_console_kind_t kind, pm_metal_runtime_handle_t handle,
			   const char *label, pm_metal_console_sink_t *out);
void pm_metal_console_close(pm_metal_console_sink_t *sink);

/* impl: bind — src/linux/pymergetic/metal/console/console.c
 *              src/zephyr/pymergetic/metal/console/console.c (stub — deferred with the rest of zephyr, see docs/RUNTIME.md "Bring-up plan")
 *
 * Wakes up a consumer currently blocked reading sink->in with EOF,
 * without otherwise touching or closing the rest of the sink (see
 * close() above for full teardown) — the "stop feeding this sink so a
 * caller's own dispatcher thread reading it can actually be joined"
 * half of shutdown (see app/app.c's console-mode teardown, the one
 * caller today). On linux: closes viewport_feed_fd (the write end of
 * sink->in's pipe) and marks it -1, so a later close() above does not
 * double-close it — kept here, not inlined at the one call site, so
 * that caller stays impl: common with no raw fd/`close()` of its own.
 * Idempotent: safe to call more than once, or on a sink that never had
 * this fd. */
void pm_metal_console_stop_feed(pm_metal_console_sink_t *sink);

#endif /* PYMERGETIC_METAL_CONSOLE_CONSOLE_H_ */
