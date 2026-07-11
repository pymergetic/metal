/*
 * Console — viewports. Host-only, port-specific (see docs/CONSOLE.md).
 *
 * A viewport renders (and, for LOCAL, injects input into) a *set* of
 * registered sinks — it owns focus/multiplexing, console.h does not. Two
 * kinds, matrix-shaped against sinks (any sink may be registered with
 * either or both viewports at once — see docs/CONSOLE.md "Sinks × viewports"):
 *
 *   PM_METAL_VIEWPORT_LOCAL   — one focused sink at a time, mirrored onto
 *     the real terminal (tty today; serial/SDL are later per-port binds of
 *     this same contract). Every *registered* sink's output keeps draining
 *     into a small backlog ring whether or not it is focused, so a chatty
 *     unfocused module never blocks on a full pipe (see console.h) — only
 *     the *focused* one is echoed live. Real terminal input is routed to
 *     whichever sink is currently focused — including any command text a
 *     human might type, so a bind's LOCAL always reserves *some* escape
 *     mechanism to get back to the kernel pane without it being swallowed
 *     as that handle's own stdin; see the linux bind's file header for the
 *     one it uses (Ctrl-A) — a per-bind detail, not part of this contract,
 *     since it depends on what "the real terminal" even is on that port
 *     (tty escape byte vs. e.g. a dedicated SDL hotkey later).
 *
 *   PM_METAL_VIEWPORT_NETWORK — every registered sink, filtered, shipped
 *     to a remote collector — no local focus concept, nothing to inject
 *     input into. Stub today (see docs/CONSOLE.md "Not yet") — the
 *     interface exists so a real implementation is additive later, but
 *     init()/pump() intentionally do nothing yet.
 */
#ifndef PYMERGETIC_METAL_CONSOLE_VIEWPORT_H_
#define PYMERGETIC_METAL_CONSOLE_VIEWPORT_H_

#include "pymergetic/metal/console/console.h"
#include "pymergetic/metal/util/log.h"

typedef enum pm_metal_viewport_kind {
	PM_METAL_VIEWPORT_LOCAL = 0,
	PM_METAL_VIEWPORT_NETWORK,
} pm_metal_viewport_kind_t;

/* impl: common — src/common/pymergetic/metal/console/viewport.c
 *
 * Registration/focus/filter bookkeeping is pure struct-and-mutex logic
 * with zero OS dependency, so — unlike pump() below — it has exactly one
 * implementation shared by every target (see docs/CONSOLE.md "What's
 * common vs bind"), not a per-plat bind.
 *
 * init(): call once per kind before register()/focus()/pump(). LOCAL
 * starts with no sink focused — the caller registers the kernel sink and
 * focus()es it as its first two calls (see docs/CONSOLE.md). Returns 0/-1.
 * shutdown(): drops every registration; does not close the sinks
 * themselves (console_close() is the caller's job, same lifetime rule as
 * every other resource in this codebase — the owner that opened it closes
 * it). */
int pm_metal_viewport_init(pm_metal_viewport_kind_t kind);
void pm_metal_viewport_shutdown(pm_metal_viewport_kind_t kind);

/* impl: common — src/common/pymergetic/metal/console/viewport.c
 *
 * register(): start tracking this sink's output (LOCAL: draining into its
 * backlog ring; NETWORK: stub, no-op today). A sink may be registered with
 * both kinds at once — they are independent subscriber lists over the same
 * console.h sinks, see docs/CONSOLE.md "Sinks × viewports". unregister()
 * before pm_metal_console_close()ing a sink — the viewport does not own
 * the sink and never closes it itself. Returns 0/-1 (LOCAL: -1 if already
 * at PM_METAL_VIEWPORT_LOCAL_MAX_SINKS). */
#define PM_METAL_VIEWPORT_LOCAL_MAX_SINKS 16
int pm_metal_viewport_register(pm_metal_viewport_kind_t kind, pm_metal_console_sink_t *sink);
void pm_metal_viewport_unregister(pm_metal_viewport_kind_t kind, pm_metal_console_sink_t *sink);

/* impl: common — src/common/pymergetic/metal/console/viewport.c
 *
 * LOCAL only (a no-op on NETWORK, which has no focus concept) — which
 * registered sink's output is mirrored onto the real terminal, and which
 * sink real terminal input is routed to. `sink` must already be
 * registered. Switching flushes the newly-focused sink's backlog
 * immediately (via port/term.h, the one OS-specific call this otherwise
 * common function needs), so nothing it printed while unfocused stays
 * hidden. Returns 0/-1. */
int pm_metal_viewport_focus(pm_metal_viewport_kind_t kind, pm_metal_console_sink_t *sink);

/* impl: common — src/common/pymergetic/metal/console/viewport.c
 *
 * Per-*pane* display filter — independent of util/log.h's global capture
 * floor (see there: that one decides what's captured at all; this one
 * decides what a viewport echoes/forwards of what was already captured).
 * Parses the "[LEVEL] " prefix pm_metal_util_log_write() writes back out
 * of each line; a line without that exact recognized prefix (e.g. a
 * guest's own free-form printf not using log.h) always passes through
 * unfiltered — it cannot honestly be classified, so it is never hidden by
 * guesswork. LOCAL: applies per-registered-sink (pass sink=NULL to set the
 * default new-registration floor instead of one sink's); NETWORK: applies
 * globally across every sink it forwards (sink is ignored — stub today). */
void pm_metal_viewport_set_filter(pm_metal_viewport_kind_t kind, pm_metal_console_sink_t *sink,
				   pm_metal_log_level_t floor);

/* impl: bind — src/linux/pymergetic/metal/console/viewport.c
 *              src/zephyr/pymergetic/metal/console/viewport.c (stub — deferred with the rest of zephyr)
 *
 * Unlike everything above, pump() genuinely differs per target: it's the
 * one place this module still does raw I/O (poll(2)+read(2)/write(2) on
 * linux) against console.h's sink fds and the real terminal — see
 * docs/CONSOLE.md "What's common vs bind". Services whatever is ready
 * right now and returns — call in a tight loop. LOCAL: waits (bounded —
 * a call may return having done nothing, this is not an error) until the
 * real terminal has input or a registered sink has output, then
 * drains/echoes/routes exactly that and returns 0. The bounded wait (not
 * infinite) is deliberate: it lets a caller interleave its own "should I
 * stop" check between calls instead of needing a self-pipe/wakeup
 * primitive — see app/app.c's pm_metal_app_run_console() shutdown path
 * for the pattern. Returns -1 if the caller should stop pumping entirely (real
 * stdin hit EOF). NETWORK: stub — returns 0 immediately, does nothing
 * (see docs/CONSOLE.md "Not yet"). */
int pm_metal_viewport_pump(pm_metal_viewport_kind_t kind);

#endif /* PYMERGETIC_METAL_CONSOLE_VIEWPORT_H_ */
