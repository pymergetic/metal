/*
 * Slim EFI WAMR runner — host-only.
 *
 * impl: common — src/pymergetic/metal/guest/wasm/wasm.c (+ embed_mods.inc.c)
 * impl: platform — src/pymergetic/metal/guest/wamr/ (efi_*.c)
 */
#ifndef PYMERGETIC_METAL_GUEST_WASM_WASM_H_
#define PYMERGETIC_METAL_GUEST_WASM_WASM_H_

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/shell/ui/ui.h> /* IWYU pragma: keep */

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/** Init WAMR pool + register Metal natives. Returns 0 ok. */
int pm_metal_wasm_init(void);

void pm_metal_wasm_shutdown(void);

int pm_metal_wasm_ready(void);

/**
 * Bind WASI stdout/stderr for the next/current run to this UI tab handle.
 * Pass PM_METAL_UI_HANDLE_INVALID to clear.
 */
void pm_metal_wasm_set_stdout_tab(pm_metal_ui_handle_t tab);

pm_metal_ui_handle_t pm_metal_wasm_stdout_tab(void);

/**
 * Look up an embedded mod by short name ("hello", "ui_hello").
 * Returns 0 and sets out bytes/len pointers, or -1 if unknown.
 */
int pm_metal_wasm_mod_lookup(const char *name, const uint8_t **bytes,
			     uint32_t *len);

/**
 * Load + instantiate. Sync mods use execute_main; async mods start a
 * long-lived session (shell_poll finishes them) unless they complete
 * during the brief startup pump.
 * Returns guest exit code (0..255), or -1 on host/load error.
 */
int pm_metal_wasm_run_bytes(const char *name, const uint8_t *bytes, uint32_t len);

/** Embed table, else ESP mods/apps/<name>/<name>.wasm. */
int pm_metal_wasm_run_mod(const char *name);

/** Pump live async session; 1 finished ok, -1 finished error, 0 still running. */
int pm_metal_wasm_session_poll(int32_t *status_out);

/** Block-pump until session done or max_ms. */
int pm_metal_wasm_session_await(uint32_t max_ms);

int pm_metal_wasm_session_active(void);

const char *pm_metal_wasm_session_name(void);

/** Tear down live wasm instance + async session (reaps process). */
void pm_metal_wasm_live_finish(void);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_WASM_WASM_H_ */
