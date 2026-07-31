/*
 * littlefs LFS_CONFIG — freestanding-friendly utils (no stdio/assert).
 * Host: system malloc. Firmware: pm_metal_mem_alloc/free.
 */
#ifndef PM_METAL_FS_LITTLEFS_LFS_CONFIG_H_
#define PM_METAL_FS_LITTLEFS_LFS_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LFS_TRACE
#define LFS_TRACE(...)
#endif
#ifndef LFS_DEBUG
#define LFS_DEBUG(...)
#endif
#ifndef LFS_WARN
#define LFS_WARN(...)
#endif
#ifndef LFS_ERROR
#define LFS_ERROR(...)
#endif
#ifndef LFS_ASSERT
#define LFS_ASSERT(test) ((void)0)
#endif

static inline uint32_t lfs_max(uint32_t a, uint32_t b) {
    return (a > b) ? a : b;
}

static inline uint32_t lfs_min(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static inline uint32_t lfs_aligndown(uint32_t a, uint32_t alignment) {
    return a - (a % alignment);
}

static inline uint32_t lfs_alignup(uint32_t a, uint32_t alignment) {
    return lfs_aligndown(a + alignment - 1, alignment);
}

static inline uint32_t lfs_npw2(uint32_t a) {
#if defined(__GNUC__)
    return 32u - (uint32_t)__builtin_clz(a - 1u);
#else
    uint32_t r = 0;
    uint32_t s;
    a -= 1u;
    s = (a > 0xffffu) << 4;
    a >>= s;
    r |= s;
    s = (a > 0xffu) << 3;
    a >>= s;
    r |= s;
    s = (a > 0xfu) << 2;
    a >>= s;
    r |= s;
    s = (a > 0x3u) << 1;
    a >>= s;
    r |= s;
    return (r | (a >> 1)) + 1u;
#endif
}

static inline uint32_t lfs_ctz(uint32_t a) {
#if defined(__GNUC__)
    return (uint32_t)__builtin_ctz(a);
#else
    return lfs_npw2((a & (uint32_t)-(int32_t)a) + 1u) - 1u;
#endif
}

static inline uint32_t lfs_popc(uint32_t a) {
#if defined(__GNUC__)
    return (uint32_t)__builtin_popcount(a);
#else
    a = a - ((a >> 1) & 0x55555555u);
    a = (a & 0x33333333u) + ((a >> 2) & 0x33333333u);
    return (((a + (a >> 4)) & 0xf0f0f0fu) * 0x1010101u) >> 24;
#endif
}

static inline int lfs_scmp(uint32_t a, uint32_t b) {
    return (int)(unsigned)(a - b);
}

static inline uint32_t lfs_fromle32(uint32_t a) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return a;
#else
    return (((uint8_t *)&a)[0] << 0) | (((uint8_t *)&a)[1] << 8) |
           (((uint8_t *)&a)[2] << 16) | (((uint8_t *)&a)[3] << 24);
#endif
}

static inline uint32_t lfs_tole32(uint32_t a) {
    return lfs_fromle32(a);
}

static inline uint32_t lfs_frombe32(uint32_t a) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#if defined(__GNUC__)
    return __builtin_bswap32(a);
#else
    return (((uint8_t *)&a)[0] << 24) | (((uint8_t *)&a)[1] << 16) |
           (((uint8_t *)&a)[2] << 8) | (((uint8_t *)&a)[3] << 0);
#endif
#else
    return a;
#endif
}

static inline uint32_t lfs_tobe32(uint32_t a) {
    return lfs_frombe32(a);
}

/* Software CRC-32 (poly reflected) — from upstream lfs_util.c. */
static inline uint32_t lfs_crc(uint32_t crc, const void *buffer, size_t size) {
    static const uint32_t rtable[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu, 0x76dc4190u,
        0x6b6b51f4u, 0x4db26158u, 0x5005713cu, 0xedb88320u, 0xf00f9344u,
        0xd6d6a3e8u, 0xcb61b38cu, 0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u,
        0xbdbdf21cu,
    };
    const uint8_t *data = (const uint8_t *)buffer;
    size_t         i;

    for (i = 0; i < size; i++) {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xfu];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xfu];
    }
    return crc;
}

#if defined(PM_METAL_LFS_FREESTANDING)
uint8_t *pm_metal_mem_alloc(size_t size);
void pm_metal_mem_free(uint8_t *ptr);

static inline void *lfs_malloc(size_t size) {
    return (void *)pm_metal_mem_alloc(size);
}

static inline void lfs_free(void *p) {
    pm_metal_mem_free((uint8_t *)p);
}
#else
#include <stdlib.h>

static inline void *lfs_malloc(size_t size) {
    return malloc(size);
}

static inline void lfs_free(void *p) {
    free(p);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_FS_LITTLEFS_LFS_CONFIG_H_ */
