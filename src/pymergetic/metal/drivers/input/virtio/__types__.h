/* pymergetic.metal.drivers.input.virtio — virtio-input events (keyboard + tablet). */
#ifndef PYMERGETIC_METAL_DRIVERS_INPUT_VIRTIO_TYPES_H
#define PYMERGETIC_METAL_DRIVERS_INPUT_VIRTIO_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PM_METAL_VIRTIO_INPUT_EV_SYN = 0,
    PM_METAL_VIRTIO_INPUT_EV_KEY = 1,
    PM_METAL_VIRTIO_INPUT_EV_ABS = 3,
    PM_METAL_VIRTIO_INPUT_KEY_A = 30,
    PM_METAL_VIRTIO_INPUT_KEY_F1 = 59,
    PM_METAL_VIRTIO_INPUT_ABS_X = 0,
    PM_METAL_VIRTIO_INPUT_ABS_Y = 1,
    PM_METAL_VIRTIO_INPUT_BTN_LEFT = 0x110,
    PM_METAL_VIRTIO_INPUT_BTN_TOUCH = 0x14a,
};

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DRIVERS_INPUT_VIRTIO_TYPES_H */
