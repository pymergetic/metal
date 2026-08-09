#ifndef PYMERGETIC_METAL_RT_INIT_H_
#define PYMERGETIC_METAL_RT_INIT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* halt/panic* do not return on firmware; int32_t face matches glue callers. */
int32_t pm_metal_rt_halt(void);
int32_t pm_metal_rt_panic(const uint8_t *msg);
int32_t pm_metal_rt_panic_at(const uint8_t *file, uint32_t line, const uint8_t *msg);
int32_t pm_metal_rt_register_symbols(void);
int32_t pm_metal_rt_connect_symbols(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_RT_INIT_H_ */
