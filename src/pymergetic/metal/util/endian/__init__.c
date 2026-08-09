/*
 * util/endian — exported wrappers over *_inline (see __init__.h).
 */
#include <pymergetic/metal/util/endian/__init__.h>

int pm_metal_util_endian_host_is_le(void)
{
  return pm_metal_util_endian_host_is_le_inline();
}

uint16_t pm_metal_util_endian_load_u16_le(const uint8_t src[2])
{
  return pm_metal_util_endian_load_u16_le_inline(src);
}

void pm_metal_util_endian_store_u16_le(uint8_t dst[2], uint16_t v)
{
  pm_metal_util_endian_store_u16_le_inline(dst, v);
}

uint32_t pm_metal_util_endian_load_u32_le(const uint8_t src[4])
{
  return pm_metal_util_endian_load_u32_le_inline(src);
}

void pm_metal_util_endian_store_u32_le(uint8_t dst[4], uint32_t v)
{
  pm_metal_util_endian_store_u32_le_inline(dst, v);
}

uint64_t pm_metal_util_endian_load_u64_le(const uint8_t src[8])
{
  return pm_metal_util_endian_load_u64_le_inline(src);
}

void pm_metal_util_endian_store_u64_le(uint8_t dst[8], uint64_t v)
{
  pm_metal_util_endian_store_u64_le_inline(dst, v);
}
