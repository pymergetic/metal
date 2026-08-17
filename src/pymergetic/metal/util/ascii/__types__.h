/* pymergetic.metal.util.ascii — FIGlet "small" render + ANSI emit. */
#ifndef PYMERGETIC_METAL_UTIL_ASCII_TYPES_H
#define PYMERGETIC_METAL_UTIL_ASCII_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PM_METAL_UTIL_ASCII_STYLE_DEFAULT = 0,
    PM_METAL_UTIL_ASCII_STYLE_DIM = 1,
    PM_METAL_UTIL_ASCII_STYLE_OK = 2,
    PM_METAL_UTIL_ASCII_STYLE_WARN = 3,
    PM_METAL_UTIL_ASCII_STYLE_FAIL = 4,
    PM_METAL_UTIL_ASCII_STYLE_ACCENT = 5,
};

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_ASCII_TYPES_H */
