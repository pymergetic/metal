/*
 * Port contract — block the calling thread for at least `ms`
 * milliseconds. Backs the shell's `sleep` builtin (see
 * shell/commands/sleep.c) — the one other place this codebase needs a
 * raw "pause for a while" primitive, kept out of common code since the
 * underlying call is OS-specific (nanosleep() on linux; k_sleep() on
 * zephyr later).
 */
#ifndef PYMERGETIC_METAL_PORT_SLEEP_H_
#define PYMERGETIC_METAL_PORT_SLEEP_H_

#include <stdint.h>

/* impl: bind — src/linux/pymergetic/metal/port/sleep.c (nanosleep())
 *              src/zephyr/pymergetic/metal/port/sleep.c (stub — deferred with
 *              the rest of zephyr's console/shell, see docs/RUNTIME.md
 *              "Bring-up plan" §5)
 *
 * Best-effort, no interrupt contract of its own: on linux this may
 * technically return early on EINTR (not looped/retried here), but a
 * process-directed signal is not guaranteed to land on the thread that
 * called this — do not rely on that to make a long call here
 * Ctrl+C-responsive. A caller that needs this to actually be
 * interruptible (e.g. shell/commands/sleep.c) must chunk it itself and
 * poll port/intr.h's pm_metal_port_intr_requested() between chunks —
 * that flag is set once, process-wide, from whichever thread the
 * signal actually hit, so any thread can observe it regardless of
 * which one this call itself happens to run on. No return value to
 * check — same "best-effort, nothing sane to do about a short sleep"
 * spirit as port/term.h's write(). */
void pm_metal_port_sleep_ms(uint32_t ms);

#endif /* PYMERGETIC_METAL_PORT_SLEEP_H_ */
