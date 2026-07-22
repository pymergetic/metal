/*
 * Metal random + wall clock — sync get (guest/host dual ABI).
 *
 * impl: common — src/pymergetic/metal/dev/random/random.c
 * impl: port  — src/{bios,efi}/pymergetic/metal/dev/random/random_port.c
 */
#ifndef PYMERGETIC_METAL_DEV_RANDOM_RANDOM_H_
#define PYMERGETIC_METAL_DEV_RANDOM_RANDOM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_RANDOM_WASI_MODULE "pymergetic.metal.random"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_RANDOM_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_RANDOM_WASI_MODULE, name)

/** Fill guest buffer; returns bytes written. */
extern uint32_t pm_metal_random(uint32_t dest, uint32_t len)
	PM_METAL_RANDOM_IMPORT(pm_metal_random);
/** Wall-clock milliseconds since Unix epoch (best effort). */
extern uint64_t pm_metal_realtime_ms(void)
	PM_METAL_RANDOM_IMPORT(pm_metal_realtime_ms);
#else
uint32_t pm_metal_random(void *dest, uint32_t len);
uint64_t pm_metal_realtime_ms(void);

int pm_metal_random_native_register(void);
void pm_metal_random_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_RANDOM_RANDOM_H_ */
