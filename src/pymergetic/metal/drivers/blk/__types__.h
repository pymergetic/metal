/* pymergetic.metal.drivers.blk — block class (bind / unbind, N handles). */
#ifndef PYMERGETIC_METAL_DRIVERS_BLK_TYPES_H
#define PYMERGETIC_METAL_DRIVERS_BLK_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_blk_ops {
    int32_t (*ready)(void *ctx);
    uint64_t (*capacity)(void *ctx);
    int32_t (*read)(void *ctx, uint64_t lba, void *buf, uint32_t nsec);
    int32_t (*write)(void *ctx, uint64_t lba, const void *buf, uint32_t nsec);
    void (*close)(void *ctx);
    void *ctx;
} pm_metal_blk_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DRIVERS_BLK_TYPES_H */
