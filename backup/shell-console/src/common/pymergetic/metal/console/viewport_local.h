/*
 * Console — LOCAL viewport internals, shared only between viewport.c
 * (impl: common, owns all the state this declares) and each target's
 * bind viewport.c (the poll/read/write loop — see docs/CONSOLE.md).
 * NOT part of console/viewport.h's public contract — nothing outside
 * this module's own two halves should include this.
 */
#ifndef PYMERGETIC_METAL_CONSOLE_VIEWPORT_LOCAL_H_
#define PYMERGETIC_METAL_CONSOLE_VIEWPORT_LOCAL_H_

#include <stddef.h>

#include "pymergetic/metal/console/console.h"
#include "pymergetic/metal/console/viewport.h"

/* impl: common — src/common/pymergetic/metal/console/viewport.c */
int pm_metal_viewport_local_is_initialized(void);

typedef struct pm_metal_viewport_local_snapshot {
	pm_metal_console_sink_t *sink[PM_METAL_VIEWPORT_LOCAL_MAX_SINKS];
} pm_metal_viewport_local_snapshot_t;

/* impl: common — src/common/pymergetic/metal/console/viewport.c
 *
 * Fills *out with every currently-registered sink (NULL for unused
 * slots), taken under the lock. Bind's pump() uses this to build its
 * pollfd array *outside* the lock, since poll() itself can block. */
void pm_metal_viewport_local_snapshot(pm_metal_viewport_local_snapshot_t *out);

/* impl: common — src/common/pymergetic/metal/console/viewport.c
 *
 * `data`/`len` came from a real read() of slot `slot_index`'s
 * sink->viewport_drain_fd; `expect` is the sink pointer the caller's
 * earlier snapshot said lived there. Re-validates identity under the
 * lock (a register()/unregister() may have raced the caller's *unlocked*
 * poll() and reused/dropped that slot) before feeding the bytes into
 * that slot's line-buffering + ring/echo logic; a mismatch is silently
 * dropped — same "next pump() call sorts it out" contract as before this
 * split. */
void pm_metal_viewport_local_on_sink_bytes(int slot_index, pm_metal_console_sink_t *expect, const char *data,
					    size_t len);

/* impl: common — src/common/pymergetic/metal/console/viewport.c
 *
 * `data`/`len` came from a real read() of the real terminal's stdin. If
 * data[0] is the escape byte (Ctrl-A — see console/viewport.h's LOCAL
 * doc comment and the linux bind's file header), switches focus to slot
 * 0 and flushes its ring via pm_metal_port_term_write(), then returns 1
 * (one byte consumed); otherwise returns 0. Caller forwards
 * data[consumed..len) itself, via pm_metal_viewport_local_focused_sink()
 * below — that forward is a raw fd write, still the bind layer's job
 * (console.h's sink fds are POSIX-shaped today; Zephyr's future
 * k_pipe-backed sinks would need a different call there entirely, see
 * console.h's zephyr stub). */
size_t pm_metal_viewport_local_consume_escape(const char *data, size_t len);

/* impl: common — src/common/pymergetic/metal/console/viewport.c
 *
 * Currently-focused sink, or NULL if none — for the bind pump() loop to
 * forward already-escape-stripped stdin bytes to. Racing a concurrent
 * focus() call is tolerated (best-effort, same spirit as every other
 * real-terminal write in this file). */
pm_metal_console_sink_t *pm_metal_viewport_local_focused_sink(void);

#endif /* PYMERGETIC_METAL_CONSOLE_VIEWPORT_LOCAL_H_ */
