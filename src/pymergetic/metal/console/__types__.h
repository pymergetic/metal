/* pymergetic.metal.console — N text rings (F1–F6) + viewports (serial + fb). */
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
