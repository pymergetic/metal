/*
 * LZ4 raw-block (browser/wasm seat) — same ABI as util/lz4 Rust on firmware.
 * Public face: include/pymergetic/metal/util/lz4/__init__.h
 */
#include <pymergetic/metal/util/lz4/__init__.h>

size_t pm_metal_util_lz4_compress_bound(size_t src_len)
{
    if (src_len > (size_t)0x7fffffff) {
        return 0;
    }
    return src_len + (src_len / 255u) + 16u;
}

int32_t pm_metal_util_lz4_compress(const uint8_t *src, size_t src_len, uint8_t *dst,
                                   size_t dst_cap)
{
    /* Compress lives in Rust on firmware; browser seat: not yet. */
    (void)src;
    (void)src_len;
    (void)dst;
    (void)dst_cap;
    return -1;
}

int32_t pm_metal_util_lz4_decompress_safe(const uint8_t *src, size_t src_len, uint8_t *dst,
                                          size_t dst_cap, size_t *out_len)
{
    size_t si = 0;
    size_t di = 0;

    if (src == NULL || dst == NULL || out_len == NULL) {
        return -1;
    }
    if (src_len == 0u) {
        *out_len = 0;
        return 0;
    }

    for (;;) {
        uint8_t token;
        size_t lit_len;
        size_t match_len;
        size_t offset;
        size_t mpos;

        if (si >= src_len) {
            return -1;
        }
        token = src[si++];
        lit_len = (size_t)(token >> 4);
        if (lit_len == 15u) {
            for (;;) {
                uint8_t b;
                if (si >= src_len) {
                    return -1;
                }
                b = src[si++];
                lit_len += (size_t)b;
                if (b != 255u) {
                    break;
                }
            }
        }
        if (lit_len > 0u) {
            size_t i;
            if (si + lit_len > src_len || di + lit_len > dst_cap) {
                return -1;
            }
            for (i = 0; i < lit_len; i++) {
                dst[di + i] = src[si + i];
            }
            si += lit_len;
            di += lit_len;
        }
        if (si >= src_len) {
            *out_len = di;
            return 0;
        }
        if (si + 2u > src_len) {
            return -1;
        }
        offset = (size_t)src[si] | ((size_t)src[si + 1u] << 8);
        si += 2u;
        if (offset == 0u || offset > di) {
            return -1;
        }
        match_len = (size_t)(token & 0x0fu);
        if (match_len == 15u) {
            for (;;) {
                uint8_t b;
                if (si >= src_len) {
                    return -1;
                }
                b = src[si++];
                match_len += (size_t)b;
                if (b != 255u) {
                    break;
                }
            }
        }
        match_len += 4u;
        if (di + match_len > dst_cap) {
            return -1;
        }
        mpos = di - offset;
        while (match_len-- > 0u) {
            dst[di++] = dst[mpos++];
        }
    }
}
