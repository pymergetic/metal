#ifndef PYMERGETIC_METAL_WAMR_HOST_H_
#define PYMERGETIC_METAL_WAMR_HOST_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_wasm_fetch_register(const uint8_t *full_module, const char *url, const uint8_t *sig,
                                     uint32_t sig_len);
int32_t pm_metal_wasm_proof_fetch(void);
uint32_t pm_metal_wasm_guest_coro_create_for(const uint8_t *full_module, uint32_t state_bytes);
int32_t pm_metal_wasm_ready(void);
int32_t pm_metal_wasm_init(void);
void pm_metal_wasm_shutdown(void);
int32_t pm_metal_wasm_load(const uint8_t *full_module, const uint8_t *bytes, uint32_t len);
int32_t pm_metal_wasm_image(const uint8_t *full_module, const uint8_t **out_bytes, uint32_t *out_len);
int32_t pm_metal_wasm_register(const uint8_t *full_module);
int32_t pm_metal_wasm_load_register(const uint8_t *full_module, const uint8_t *bytes, uint32_t len);
int32_t pm_metal_wasm_load_verified(const uint8_t *full_module, const uint8_t *bytes, uint32_t len,
                                    const uint8_t *sig, uint32_t sig_len);
int32_t pm_metal_wasm_unload(const uint8_t *full_module);
int32_t pm_metal_wasm_call0(const uint8_t *full_module, const uint8_t *func);
int32_t pm_metal_wasm_proof_stress(void);
int32_t pm_metal_wasm_proof(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_WAMR_HOST_H_ */
