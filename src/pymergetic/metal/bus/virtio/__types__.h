/* pymergetic.metal.bus.virtio — transport IDs (not a NIC). */
#ifndef PYMERGETIC_METAL_BUS_VIRTIO_TYPES_H
#define PYMERGETIC_METAL_BUS_VIRTIO_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PM_METAL_BUS_VIRTIO_VENDOR = 0x1af4,
    PM_METAL_BUS_VIRTIO_DEV_NET_LEGACY = 0x1000,
    PM_METAL_BUS_VIRTIO_DEV_BLK_LEGACY = 0x1001,
    PM_METAL_BUS_VIRTIO_DEV_NET = 0x1041,
    PM_METAL_BUS_VIRTIO_DEV_BLK = 0x1042,
    PM_METAL_BUS_VIRTIO_DEV_GPU_LEGACY = 0x1010,
    PM_METAL_BUS_VIRTIO_DEV_GPU = 0x1050,
    PM_METAL_BUS_VIRTIO_DEV_INPUT = 0x1052,
};

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BUS_VIRTIO_TYPES_H */
