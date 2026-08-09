#ifndef PM_METAL_RT_H_
#define PM_METAL_RT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Freestanding Rust callee (crates/pymergetic_metal_rt). */

_Noreturn void pm_metal_rt_halt(void);
_Noreturn void pm_metal_rt_panic(const uint8_t *msg);
_Noreturn void pm_metal_rt_panic_at(const uint8_t *file, uint32_t line, const uint8_t *msg);

/* Registry bind (returns -1 until product links `reg`). */
int32_t pm_metal_rt_register_symbols(void);
/* No-op connect; console/mem are direct extern "C". */
int32_t pm_metal_rt_connect_symbols(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_RT_H_ */
