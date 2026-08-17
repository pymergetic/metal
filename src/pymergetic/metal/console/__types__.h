/* pymergetic.metal.console — text ring + viewports (serial today, FB later). */
#ifndef PYMERGETIC_METAL_CONSOLE_TYPES_H
#define PYMERGETIC_METAL_CONSOLE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_console_sink_fn)(const char *s, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_CONSOLE_TYPES_H */
