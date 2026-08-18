/* pymergetic.metal.drivers.input — input class (bind / poll / inject, N handles). */
#ifndef PYMERGETIC_METAL_DRIVERS_INPUT_TYPES_H
#define PYMERGETIC_METAL_DRIVERS_INPUT_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_input_ops {
    int32_t (*open)(void *ctx);
    void (*close)(void *ctx);
    int32_t (*poll)(void *ctx);
    int32_t (*inject)(void *ctx, int32_t key);
    void *ctx;
} pm_metal_input_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DRIVERS_INPUT_TYPES_H */
