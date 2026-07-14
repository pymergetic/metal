/*
 * App — the whole-process scripted run mode, librarified out of what
 * used to be src/linux/main.c's own body. impl: common — nothing here is
 * OS-specific by itself, so any future target's own main() can reach the
 * exact same mode just by calling into this module, instead of
 * re-implementing it.
 *
 * What stays in each target's own main.c: argv/Kconfig parsing into a
 * pm_metal_runtime_config_t, pm_metal_runtime_init()/pm_metal_process_init(),
 * any startup diagnostics printf, then one call into this module — see
 * src/linux/main.c for the reference shape. This function does not call
 * pm_metal_runtime_init()/pm_metal_process_init() itself (the caller
 * already did), but leaves the runtime/process layer fully shut down
 * again before returning — same "whoever opens something closes it"
 * rule as everywhere else in this codebase, just at whole-process
 * granularity here.
 */
#ifndef PYMERGETIC_METAL_APP_APP_H_
#define PYMERGETIC_METAL_APP_APP_H_

/* impl: common — src/common/pymergetic/metal/app/app.c
 *
 * Loads, runs (argv[0] = its basename), and unloads each of
 * wasm_argv[0..wasm_argc), in order, via runtime/process.h's spawn()+wait()
 * — sequential/blocking, one after another. Logs "<path>: exit=%d" to
 * real stdout per module (fflush()ed immediately). Calls
 * pm_metal_process_shutdown() + pm_metal_runtime_shutdown() itself before
 * returning, whether or not every module succeeded. Returns 0 if every
 * module exited 0, else 1 — never -1/negative, safe to return straight
 * from main(). `argv0` is used only in its own stderr diagnostics
 * ("<argv0>: load failed: <path>", etc). */
int pm_metal_app_run_scripted(const char *argv0, int wasm_argc, char **wasm_argv);

#endif /* PYMERGETIC_METAL_APP_APP_H_ */
