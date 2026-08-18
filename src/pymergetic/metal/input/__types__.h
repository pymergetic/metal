/* pymergetic.metal.input — key ring; F1–F6 select consoles. */
#ifndef PYMERGETIC_METAL_INPUT_TYPES_H
#define PYMERGETIC_METAL_INPUT_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PM_METAL_INPUT_KEY_F1 = 0x101,
    PM_METAL_INPUT_KEY_F2 = 0x102,
    PM_METAL_INPUT_KEY_F3 = 0x103,
    PM_METAL_INPUT_KEY_F4 = 0x104,
    PM_METAL_INPUT_KEY_F5 = 0x105,
    PM_METAL_INPUT_KEY_F6 = 0x106,
};

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_INPUT_TYPES_H */
