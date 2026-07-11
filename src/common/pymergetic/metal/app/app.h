/*
 * App — the two whole-process run modes (console, scripted), librarified
 * out of what used to be src/linux/main.c's own body. impl: common —
 * nothing here is OS-specific by itself (the one thread and the one raw
 * fd this used to touch directly now go through port/worker.h and
 * console.h's own pm_metal_console_stop_feed(), respectively), so any
 * future target's own main() can reach the exact same two modes just by
 * calling into this module, instead of re-implementing them.
 *
 * What stays in each target's own main.c: argv/Kconfig parsing into a
 * pm_metal_runtime_config_t, pm_metal_runtime_init()/pm_metal_process_init(),
 * any startup diagnostics printf, then one call into this module — see
 * src/linux/main.c for the reference shape. Neither function here calls
 * pm_metal_runtime_init()/pm_metal_process_init() itself (the caller
 * already did, before deciding which mode to run), but both leave the
 * runtime/process layer fully shut down again before returning — same
 * "whoever opens something closes it" rule as everywhere else in this
 * codebase, just at whole-process granularity here.
 */
#ifndef PYMERGETIC_METAL_APP_APP_H_
#define PYMERGETIC_METAL_APP_APP_H_

/* impl: common — src/common/pymergetic/metal/app/app.c
 *
 * Loads, runs (argv[0] = its basename), and unloads each of
 * wasm_argv[0..wasm_argc), in order, via runtime/process.h's spawn()+wait()
 * — sequential/blocking, one after another, no console/shell involved.
 * Logs "<path>: exit=%d" to real stdout per module (fflush()ed
 * immediately), same as the old src/linux/main.c scripted mode. Calls
 * pm_metal_process_shutdown() + pm_metal_runtime_shutdown() itself before
 * returning, whether or not every module succeeded. Returns 0 if every
 * module exited 0, else 1 — never -1/negative, safe to return straight
 * from main(). `argv0` is used only in its own stderr diagnostics
 * ("<argv0>: load failed: <path>", etc). */
int pm_metal_app_run_scripted(const char *argv0, int wasm_argc, char **wasm_argv);

/* impl: common — src/common/pymergetic/metal/app/app.c
 *
 * Opens the kernel console sink + PM_METAL_VIEWPORT_LOCAL, registers
 * every builtin (see shell/commands.h), pre-load()s wasm_argv[0..wasm_argc)
 * same as typing `load <path>` for each, then starts the kernel dispatcher
 * (its own worker thread, see port/worker.h) and pumps
 * PM_METAL_VIEWPORT_LOCAL on the calling thread until either real stdin
 * hits EOF or the `quit`/`exit` builtin fires — at which point it stops
 * feeding the dispatcher (pm_metal_console_stop_feed()), joins it, drains
 * whatever pump() output was still in flight, and tears everything down in
 * order (pm_metal_process_shutdown(), shell/commands.h's
 * handles_shutdown(), the kernel sink's close(), pm_metal_runtime_shutdown())
 * before returning 0 — blocks the calling thread for the whole run, same
 * as the old src/linux/main.c's run_console_mode()+shutdown_console_mode().
 * `vfs_root_abs` is used only in its own startup log line. Returns 1 (not
 * 0) if the console/viewport itself failed to even start — in that case
 * pm_metal_runtime_shutdown() has already run too, nothing further for the
 * caller to unwind. */
int pm_metal_app_run_console(const char *argv0, const char *vfs_root_abs, int wasm_argc, char **wasm_argv);

#endif /* PYMERGETIC_METAL_APP_APP_H_ */
