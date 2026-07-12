/*
 * Leveled log formatting — global capture floor + "[LEVEL] msg" framing.
 * Pure C, zero OS/platform dependency — same "shared" exception as
 * util/size.h (see docs/SOURCETREE.md "shared"): safe for both mods and
 * the runtime binary to call.
 *
 * This header is deliberately *not* aware of consoles/sinks/viewports
 * (see src/common/pymergetic/metal/console/{console,viewport}.h, host-only)
 * — it only decides "is this message even worth keeping" (the one global
 * floor below) and how to format what survives that check onto a FILE*
 * the caller already has open. Per-pane *display* filtering of already-
 * captured output is a separate, later-stage concern that belongs to
 * viewport.h, not here — see docs/CONSOLE.md.
 *
 * Body lives in log_impl.h, compiled once per binary via a thin loader .c
 * (src/shared/pymergetic/metal/util/log.c for the runtime; a modlib loader
 * will do the same for guests later) — never #include log_impl.h directly,
 * only this contract.
 */
#ifndef PYMERGETIC_METAL_UTIL_LOG_H_
#define PYMERGETIC_METAL_UTIL_LOG_H_

#include <stdio.h>

typedef enum pm_metal_log_level {
	PM_METAL_LOG_TRACE = 0,
	PM_METAL_LOG_DEBUG,
	PM_METAL_LOG_INFO,
	PM_METAL_LOG_WARN,
	PM_METAL_LOG_ERROR,
	PM_METAL_LOG_FATAL,
	PM_METAL_LOG_LEVEL_COUNT,
} pm_metal_log_level_t;

/* impl: shared — include/pymergetic/metal/util/log_impl.h
 *
 * The one global capture floor: pm_metal_util_log_write() drops (does not
 * format, does not touch `out` at all) any message below `level`. There is
 * exactly one floor for the whole process (kernel and every guest would
 * share it, if a guest ever gets direct access) — *not* per-sink, per-pane,
 * or per-handle; that's viewport.h's job, one layer up, on bytes that made
 * it past this floor already. Defaults to PM_METAL_LOG_INFO. Not
 * thread-safe against concurrent set_level() calls racing writes (this is
 * a coarse, rarely-changed knob — see docs/CONSOLE.md "Log level: global
 * floor vs per-pane filter"); safe to call from init()/main() before other
 * threads exist, which is the only place this codebase calls it today. */
void pm_metal_util_log_set_level(pm_metal_log_level_t level);
pm_metal_log_level_t pm_metal_util_log_get_level(void);

/* impl: shared — include/pymergetic/metal/util/log_impl.h
 *
 * "[LEVEL] " + vsnprintf(fmt, ...) + "\n" onto `out`, iff level >= the
 * global floor (checked first, before any formatting work) — otherwise a
 * complete no-op, `out` is never touched. `out` is a plain FILE* the
 * caller already owns; on the host this is normally a console sink's
 * ->out (see console/console.h) or plain stdout/stderr, on a guest it's
 * WASI's own stdout/stderr — this function does not know or care which. */
void pm_metal_util_log_write(FILE *out, pm_metal_log_level_t level, const char *fmt, ...);

/* impl: shared — include/pymergetic/metal/util/log_impl.h
 *
 * "TRACE".."FATAL", or "?" for an out-of-range value. Used both for the
 * "[LEVEL] " prefix above and by viewport.c to parse it back out of
 * already-captured lines for per-pane filtering. */
const char *pm_metal_util_log_level_name(pm_metal_log_level_t level);

/* impl: shared — include/pymergetic/metal/util/log_impl.h
 *
 * vsnprintf(fmt, ...) + "\n" onto `out`, fflush()'d — same framing as
 * pm_metal_util_log_write() minus the "[LEVEL] " prefix and the global
 * floor check (this always writes, iff `out` is non-NULL). For a
 * command's own *data* — the literal answer to what was typed (e.g.
 * `pwd`'s path, `ls`'s listing, `env`'s vars, `uname`'s one line) —
 * which a real shell never annotates with a log severity, as opposed to
 * a genuine diagnostic/event message (a confirmation, an error), which
 * should keep going through pm_metal_util_log_write() so it stays
 * leveled and filterable. This is not a loophole around the global
 * floor for diagnostics — callers that need that stay on
 * pm_metal_util_log_write(); this is only for lines that were never a
 * "log message" in the first place. A useful side effect: a line
 * written this way has no "[LEVEL] " prefix for viewport.c's per-pane
 * filter to recognize, so it always passes through unfiltered (see
 * that filter's own "no bracket -> passes through unfiltered" rule) —
 * exactly right for output the operator explicitly asked for, which a
 * pane's display filter threshold should never be able to hide. */
void pm_metal_util_log_write_raw(FILE *out, const char *fmt, ...);

#endif /* PYMERGETIC_METAL_UTIL_LOG_H_ */
