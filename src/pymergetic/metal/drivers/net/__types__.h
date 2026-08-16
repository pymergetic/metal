/* pymergetic.metal.drivers.net — netdev class (bind / unbind, N handles). */
#ifndef PYMERGETIC_METAL_DRIVERS_NET_TYPES_H
#define PYMERGETIC_METAL_DRIVERS_NET_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_netdev_ops {
    int32_t (*open)(void *ctx);
    void (*close)(void *ctx);
    void (*mac)(void *ctx, uint8_t out[6]);
    int32_t (*tx)(void *ctx, const uint8_t *frame, uint16_t len);
    int32_t (*poll)(void *ctx);
    void *ctx;
} pm_metal_netdev_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DRIVERS_NET_TYPES_H */
