/*
 * Port contract — "the operator asked us to stop right now" (Ctrl+C on a
 * real terminal; SIGINT on linux). Exists so app/app.c's console-mode
 * run loop can treat an interrupt exactly like the `quit`/`exit` builtin
 * or real stdin EOF — one more thing it polls, then runs the *same* full
 * teardown (see app.h's pm_metal_app_run_console()) — never the OS's own
 * default "just die" action, which would skip unload()ing every handle,
 * closing sinks, and pm_metal_runtime_shutdown().
 *
 * install()/requested() split the same way pm_metal_util_log_*()'s
 * level knob does: install() runs once, from the calling thread, before
 * anything else in this file is touched; requested() is then polled
 * from ordinary code — never from inside a signal handler itself, and
 * the handler this sets up (linux: sigaction(SIGINT, ...)) does nothing
 * but flip that one flag, kept async-signal-safe on purpose (see the
 * linux bind's own file header). No way to un-request or reset — once
 * set, stays set for the rest of the process; nothing today needs to
 * "arm" this more than once per run.
 */
#ifndef PYMERGETIC_METAL_PORT_INTR_H_
#define PYMERGETIC_METAL_PORT_INTR_H_

/* impl: bind — src/linux/pymergetic/metal/port/intr.c (sigaction(SIGINT, ...))
 *              src/zephyr/pymergetic/metal/port/intr.c (stub — deferred with the
 *              rest of zephyr's console/shell, see docs/RUNTIME.md "Bring-up
 *              plan" §5; requested() always returns 0 until then)
 *
 * install(): call once, before the first requested() poll — safe to call
 * from a controller thread with no console loop running yet (app.c does
 * this right before starting its pump loop). requested(): 1 once an
 * interrupt has been observed since install(), 0 otherwise — cheap
 * enough to poll every loop iteration. */
void pm_metal_port_intr_install(void);
int pm_metal_port_intr_requested(void);

#endif /* PYMERGETIC_METAL_PORT_INTR_H_ */
