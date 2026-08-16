/* pymergetic.metal.dt — live device inventory (add / unbind, N of same compatible). */
#ifndef PYMERGETIC_METAL_DT_TYPES_H
#define PYMERGETIC_METAL_DT_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PM_METAL_DT_CLASS_NET = 1,
    PM_METAL_DT_CLASS_BLK = 2,
    PM_METAL_DT_CLASS_RTC = 3,
    PM_METAL_DT_CLASS_MEM = 4,
    PM_METAL_DT_CLASS_GFX = 5,
    PM_METAL_DT_CLASS_AUDIO = 6,
    PM_METAL_DT_CLASS_INPUT = 7,
};

enum {
    PM_METAL_DT_BUS_PLATFORM = 0,
    PM_METAL_DT_BUS_PCI = 1,
    PM_METAL_DT_BUS_ISA = 2,
    PM_METAL_DT_BUS_VIRTIO = 3,
    PM_METAL_DT_BUS_MMIO = 4,
};

int32_t pm_metal_dt_loc(int32_t id, uint32_t idx, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DT_TYPES_H */
