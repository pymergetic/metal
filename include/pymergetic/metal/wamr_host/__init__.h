/*
 * Freestanding WAMR host (metal hosts guest .wasm packs on the box).
 * Not arch.wasm — that is the browser seat under pymergetic.metal.arch.wasm.
 */
#ifndef PYMERGETIC_METAL_WAMR_HOST_H_
#define PYMERGETIC_METAL_WAMR_HOST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_wasm_init(void);
int32_t pm_metal_wasm_proof(void);
int32_t pm_metal_wasm_proof_stress(void);
int32_t pm_metal_wasm_proof_fetch(void);

#ifdef __cplusplus
}
#endif

#endif
