/*
 * pymergetic.metal.util.size — binary-prefix byte formatting (RS callee, C ABI).
 */
#ifndef PYMERGETIC_METAL_UTIL_SIZE_H_
#define PYMERGETIC_METAL_UTIL_SIZE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_UTIL_SIZE_FORMAT_MAX 16

/* "88 MiB" — returns length or -1. */
int32_t pm_metal_util_size_format(uint8_t *out, size_t cap, uint64_t bytes);

/* "92946432 (88 MiB)" — returns length or -1. */
int32_t pm_metal_util_size_format_bytes(uint8_t *out, size_t cap, uint64_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_SIZE_H_ */
