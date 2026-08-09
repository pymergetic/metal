/*
 * pymergetic.metal.util.tar — ustar walk/write (RS callee, C ABI).
 */
#ifndef PYMERGETIC_METAL_UTIL_TAR_H_
#define PYMERGETIC_METAL_UTIL_TAR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callback per entry. Return 0 to continue, non-zero to abort. */
typedef int (*pm_metal_util_tar_foreach_fn)(uint8_t *ctx, const uint8_t *name, uint64_t size,
                                            int32_t is_dir, const uint8_t *data, size_t data_len);

typedef int (*pm_metal_util_tar_foreach_ex_fn)(uint8_t *ctx, const uint8_t *name, uint64_t size,
                                               int32_t is_dir, uint64_t header_off,
                                               uint64_t payload_off, const uint8_t *data,
                                               size_t data_len);

int32_t pm_metal_util_tar_foreach(const uint8_t *archive, size_t len,
                                  pm_metal_util_tar_foreach_fn cb, uint8_t *ctx);

int32_t pm_metal_util_tar_foreach_ex(const uint8_t *archive, size_t len,
                                     pm_metal_util_tar_foreach_ex_fn cb, uint8_t *ctx);

/* typeflag: '0' file, '5' dir. Returns 512 or -1. */
int32_t pm_metal_util_tar_write_header(uint8_t *out, size_t out_cap, const uint8_t *name,
                                       uint64_t size, uint8_t typeflag);

size_t pm_metal_util_tar_pad_len(uint64_t size);

int32_t pm_metal_util_tar_write_end(uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_TAR_H_ */
