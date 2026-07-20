/*
 * Metal shell — guest/host dual ABI.
 *
 * Guests may log, set status, request exit, and run/tab named mods.
 * Host-only: init/poll drive ConIn + the interactive loop.
 */
#ifndef PYMERGETIC_METAL_SHELL_H_
#define PYMERGETIC_METAL_SHELL_H_

#include <stdint.h>

#include "pymergetic/metal/wasi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_SHELL_WASI_MODULE "pymergetic.metal.shell"

#if defined(__wasm__)
#define PM_METAL_SHELL_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_SHELL_WASI_MODULE, name)
#endif

#if !defined(__wasm__)
/** Bind to UI shell (must exist). Returns 0 ok. Auto-runs embedded hello once. */
int pm_metal_shell_init(void);
/**
 * One poll: keys, cursor tick, redraw.
 * Returns 1 exit, 0 continue, -1 error.
 */
int pm_metal_shell_poll(void);
#endif

#if defined(__wasm__)
extern void pm_metal_shell_log(const char *line)
	PM_METAL_SHELL_IMPORT(pm_metal_shell_log);
extern void pm_metal_shell_set_status(const char *text)
	PM_METAL_SHELL_IMPORT(pm_metal_shell_set_status);
extern void pm_metal_shell_request_exit(void)
	PM_METAL_SHELL_IMPORT(pm_metal_shell_request_exit);
extern int pm_metal_shell_run(const char *mod)
	PM_METAL_SHELL_IMPORT(pm_metal_shell_run);
extern int pm_metal_shell_tab(const char *mod)
	PM_METAL_SHELL_IMPORT(pm_metal_shell_tab);
#else
void pm_metal_shell_log(const char *line);
void pm_metal_shell_set_status(const char *text);
void pm_metal_shell_request_exit(void);
/** Run mod in the active tab. Returns guest exit code, or -1 on host error. */
int pm_metal_shell_run(const char *mod);
/** Open a tab named after mod, run there, leave tab open. */
int pm_metal_shell_tab(const char *mod);

int pm_metal_shell_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_H_ */
