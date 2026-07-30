/*
 * Wire endianness for util fourcc / eightcc (LE on the wire).
 * Header-only — static inline load/store.
 */
#ifndef PYMERGETIC_METAL_UTIL_ENDIAN_H_
#define PYMERGETIC_METAL_UTIL_ENDIAN_H_

#include <stdint.h>

#define PM_METAL_UTIL_WIRE_ENDIAN_IS_LE 1

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
  __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define PM_METAL_UTIL_ENDIAN_HOST_IS_LE 1
#elif defined(__i386__) || defined(__x86_64__) || defined(__wasm__) || \
  defined(__ARMEL__) || defined(__AARCH64EL__)
#define PM_METAL_UTIL_ENDIAN_HOST_IS_LE 1
#else
#define PM_METAL_UTIL_ENDIAN_HOST_IS_LE 1 /* exp2 targets are LE */
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline int pm_metal_util_endian_host_is_le(void)
{
  return PM_METAL_UTIL_ENDIAN_HOST_IS_LE;
}

static inline uint16_t pm_metal_util_endian_load_u16_le(const uint8_t src[2])
{
  return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static inline void pm_metal_util_endian_store_u16_le(uint8_t dst[2], uint16_t v)
{
  dst[0] = (uint8_t)(v & 0xFFu);
  dst[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline uint32_t pm_metal_util_endian_load_u32_le(const uint8_t src[4])
{
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

static inline void pm_metal_util_endian_store_u32_le(uint8_t dst[4], uint32_t v)
{
  dst[0] = (uint8_t)(v & 0xFFu);
  dst[1] = (uint8_t)((v >> 8) & 0xFFu);
  dst[2] = (uint8_t)((v >> 16) & 0xFFu);
  dst[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline uint64_t pm_metal_util_endian_load_u64_le(const uint8_t src[8])
{
  uint64_t v = 0;
  int i;

  for (i = 0; i < 8; i++) {
    v |= (uint64_t)src[i] << (8 * i);
  }
  return v;
}

static inline void pm_metal_util_endian_store_u64_le(uint8_t dst[8], uint64_t v)
{
  int i;

  for (i = 0; i < 8; i++) {
    dst[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
  }
}

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_ENDIAN_H_ */
