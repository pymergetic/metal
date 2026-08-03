/* Metal wasm port — WAMR host face (one Metal memory). */
#ifndef PM_METAL_WASM_PORT_RUNTIME_H_
#define PM_METAL_WASM_PORT_RUNTIME_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if WAMR runtime is up. */
int32_t pm_metal_wasm_port_ready(void);

/* Init WAMR over a Metal-allocated pool. 0 ok, -1 fail. */
int32_t pm_metal_wasm_port_init(void);

/* Register kernel guest_surface natives. Called from init. */
int32_t pm_metal_wasm_port_register_host_natives(void);

/* Guest coro call-in: export name looked up on create (default "step"). */
void pm_metal_wasm_guest_callin_set_step(const char *name);

/*
 * Host: create a guest coro for a loaded module using the current callin
 * step name. Returns coro handle, or 0 on fail.
 */
uint32_t pm_metal_wasm_port_guest_coro_create(const uint8_t *full_module, uint32_t state_bytes);

void pm_metal_wasm_port_shutdown(void);

/*
 * Load + instantiate wasm bytes under full_module name (NUL C string).
 * Replaces any prior image for that name. Also reads `bytes`' own
 * "pm_metal_imports" custom section (if any -- see
 * docs/definitions/module.md "Cross-package imports") and registers a
 * forwarding native for every cross-package import it declares before
 * instantiating, so this module's own imports resolve without any
 * prior, build-time knowledge of them anywhere else. 0 ok, -1 fail.
 */
int32_t pm_metal_wasm_port_load(const uint8_t *full_module, const uint8_t *bytes, uint32_t len);

/* Unload one module by name (also releases its claimed trampolines --
 * see pm_metal_wasm_port_claim_trampoline). */
void pm_metal_wasm_port_unload(const uint8_t *full_module);

/* Call export directly (host-side convenience; does not touch reg). */
int32_t pm_metal_wasm_port_call0(const uint8_t *full_module, const uint8_t *func);

/*
 * Registry-publish discovery: how many `() -> i32` exports `full_module`
 * has, and (by filtered index `0..count`) each one's name. The Rust
 * host (`wasm::register`) uses these to size and fill a `RegEntry[]`
 * before calling `pm_metal_reg_mod_load` -- see
 * docs/definitions/module.md "wasm packages join the same registry".
 */
int32_t pm_metal_wasm_port_export_count(const uint8_t *full_module);
int32_t pm_metal_wasm_port_export_name(const uint8_t *full_module, int32_t idx, uint8_t *buf,
                                        uint32_t buf_n);

/*
 * Claim a trampoline bound to `(full_module, func)` and return its
 * `RegEntry`-publishable `() -> i32` address directly -- NULL if
 * `func` is not a publishable export. The pool is a heap-allocated
 * ring of self-contained, self-stamped nodes with no size limit (see
 * runtime_host.c's `stamp_tramp`/`grow_tramp_arena`) -- there is no
 * separate resolution step on the Rust side anymore. Freed
 * automatically (all at once) when `full_module` unloads.
 */
void *pm_metal_wasm_port_claim_trampoline(const uint8_t *full_module, const uint8_t *func);

/*
 * Borrow the loaded image bytes for `full_module` (still owned by the
 * slot). Writes pointer+len on success. 0 ok, -1 if not loaded.
 */
int32_t pm_metal_wasm_port_image(const uint8_t *full_module, const uint8_t **out_bytes,
                                 uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_WASM_PORT_RUNTIME_H_ */
