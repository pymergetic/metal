/*
 * pymergetic.metal.util.lz4 — raw LZ4 block compress/decompress.
 * Path == module. Impl: src/.../util/lz4 (RS on firmware, C twin on browser).
 */
#ifndef PYMERGETIC_METAL_UTIL_LZ4_H_
#define PYMERGETIC_METAL_UTIL_LZ4_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Upper bound on compressed size for src_len, or 0 if too large. */
size_t pm_metal_util_lz4_compress_bound(size_t src_len);

/**
 * Compress src into a raw LZ4 block in dst.
 * Returns compressed length, or -1 on error / short buffer.
 * (Firmware RS exports this; browser C twin may omit until ported.)
 */
int32_t pm_metal_util_lz4_compress(const uint8_t *src, size_t src_len, uint8_t *dst,
                                   size_t dst_cap);

/**
 * Decompress a raw LZ4 block into dst.
 * On success returns 0 and writes byte count to *out_len; else -1.
 */
int32_t pm_metal_util_lz4_decompress_safe(const uint8_t *src, size_t src_len, uint8_t *dst,
                                          size_t dst_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_LZ4_H_ */
