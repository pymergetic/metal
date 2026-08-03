/*
 * Guest wasm coroutines — call-in trampoline + linear pin of durable frame.
 */
#ifndef PM_METAL_WASM_PORT_GUEST_CORO_H_
#define PM_METAL_WASM_PORT_GUEST_CORO_H_

#include <stdint.h>

#include "wasm_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default export name looked up when creating a guest coro ("step"). */
void pm_metal_wasm_guest_callin_set_step(const char *name);

/* Create guest coro (NULL-step product shape). 0 = fail. */
uint32_t pm_metal_wasm_guest_coro_create(wasm_exec_env_t exec_env, uint32_t state_bytes);

/* Host: create guest coro for an already-loaded module instance. 0 = fail. */
uint32_t pm_metal_wasm_guest_coro_create_inst(wasm_module_inst_t inst, uint32_t state_bytes);

/* Guest dual-ABI: linear offset of pinned frame (0 if unpinned/missing). */
uint32_t pm_metal_wasm_guest_coro_state(uint32_t h);

/* Alloc/ensure durable frame; returns linear offset while pinned (pins if needed). */
uint32_t pm_metal_wasm_guest_coro_alloc(uint32_t h, uint32_t n);

void pm_metal_wasm_guest_coro_close(uint32_t h);

/*
 * From a guest native (current exec_env): create+schedule export "step"
 * (or callin step name), poll until DONE. 0 ok, -1 fail.
 */
int32_t pm_metal_wasm_guest_coro_smoke(wasm_exec_env_t exec_env);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_WASM_PORT_GUEST_CORO_H_ */
