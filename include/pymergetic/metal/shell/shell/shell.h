/*
 * Metal shell — guest/host dual ABI.
 *
 * Guests may log, set status, request exit, and run/tab named mods.
 * Host-only: init/poll drive rings + the interactive loop.
 *
 * impl: common — src/pymergetic/metal/shell/shell/shell.c
 */
#ifndef PYMERGETIC_METAL_SHELL_SHELL_H_
#define PYMERGETIC_METAL_SHELL_SHELL_H_

#include <stdint.h>

#include <pymergetic/metal/wasi.h> /* IWYU pragma: keep */

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_SHELL_WASI_MODULE "pymergetic.metal.shell"

#if defined(__wasm__)
#define PM_METAL_SHELL_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_SHELL_WASI_MODULE, name)
#endif

#if !defined(__wasm__)
#include "pymergetic/metal/runtime/async/async.h"

/** Bash-like command history capacity (oldest dropped when full). */
#define PM_METAL_SHELL_HISTORY_MAX  64u

/** Bind to UI shell chrome (must exist). Proofs are pool/init-coro work. */
int pm_metal_shell_init(void);
/**
 * Format bash-like prompt into out (e.g. "metal:~$ ").
 * Returns length excluding NUL, or 0 on error.
 */
uint32_t pm_metal_shell_prompt(char *out, uint32_t cap);
/** Record a committed command line (skips empty / consecutive dup). */
void pm_metal_shell_history_add(const char *line);
/** Number of retained history entries. */
uint32_t pm_metal_shell_history_count(void);
/**
 * Read retained entry by index (0 = oldest). Returns 0 ok, -1 bad idx.
 */
int pm_metal_shell_history_get(uint32_t idx, char *out, uint32_t cap);
/**
 * One poll: keys, cursor tick, redraw.
 * Returns 1 exit, 0 continue, -1 error.
 */
int pm_metal_shell_poll(void);
/** 1 if last exit requested reboot (`exit -r`), else 0 (power-off). */
int pm_metal_shell_exit_reboot(void);
/** Suggested coop sleep after the last poll (1 ms when busy, else ~16). */
uint32_t pm_metal_shell_pump_sleep_ms(void);

/** 1 if a background shell job (ping/nslookup/test) is already running. */
int pm_metal_shell_job_busy(void);
/**
 * After a background log line (e.g. metal-net: …) hit the UART mid-prompt,
 * ask the next shell_poll to re-offer the live prompt when idle.
 */
void pm_metal_shell_prompt_dirty(void);
/**
 * Track a host async task; shell_poll pumps it and prints on completion.
 * kind: "ping" | "nslookup" | "test". detail optional (e.g. hostname).
 * Returns 0 ok. Job poll treats WAITING like PENDING (sleep/DNS park).
 */
int pm_metal_shell_job_start(const char *kind, pm_metal_async_handle_t task_h,
			     pm_metal_async_handle_t coro_h, const char *detail,
			     uint64_t deadline_us);
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

/**
 * UART-only log (no ConOut/GOP). Prefer this while a guest owns the FB.
 * Falls back to Print if SerialIo is missing.
 */
void pm_metal_shell_serial_log(const char *line);

int pm_metal_shell_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_SHELL_H_ */
