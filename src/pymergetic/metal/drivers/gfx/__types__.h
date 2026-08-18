/* pymergetic.metal.drivers.gfx — scanout class (bind / present / poll, N handles). */
#ifndef PYMERGETIC_METAL_DRIVERS_GFX_TYPES_H
#define PYMERGETIC_METAL_DRIVERS_GFX_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_gfx_ops {
    int32_t (*open)(void *ctx);
    void (*close)(void *ctx);
    int32_t (*present)(void *ctx, const uint8_t *pix, uint32_t w, uint32_t h, uint32_t stride);
    int32_t (*poll)(void *ctx);
    int32_t (*info)(void *ctx, uint32_t *w, uint32_t *h, uint32_t *stride);
    void *ctx;
} pm_metal_gfx_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DRIVERS_GFX_TYPES_H */
