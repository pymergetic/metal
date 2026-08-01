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

void pm_metal_wasm_port_shutdown(void);

/*
 * Load + instantiate wasm bytes under full_module name (NUL C string).
 * Replaces any prior image for that name. 0 ok, -1 fail.
 */
int32_t pm_metal_wasm_port_load(const uint8_t *full_module, const uint8_t *bytes, uint32_t len);

/* Unload one module by name. */
void pm_metal_wasm_port_unload(const uint8_t *full_module);

/*
 * Publish () -> i32 exports onto reg under full_module.
 * Returns number of symbols published, or -1.
 */
int32_t pm_metal_wasm_port_publish_reg(const uint8_t *full_module);

/* Call export directly (does not require reg). */
int32_t pm_metal_wasm_port_call0(const uint8_t *full_module, const uint8_t *func);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_WASM_PORT_RUNTIME_H_ */
