/*
 * util/endian — exported wrappers over *_inline (see __init__.h).
 */
#include <pymergetic/metal/util/endian/__init__.h>
#include <pymergetic/metal/reg/mod.h>

static pm_metal_reg_export_t util_endian_exports[] = {
    PM_METAL_REG_EXPORT(host_is_le),
    PM_METAL_REG_EXPORT(load_u16_le),
    PM_METAL_REG_EXPORT(store_u16_le),
    PM_METAL_REG_EXPORT(load_u32_le),
    PM_METAL_REG_EXPORT(store_u32_le),
    PM_METAL_REG_EXPORT(load_u64_le),
    PM_METAL_REG_EXPORT(store_u64_le),
};
PM_METAL_REG_REF(util_endian, host_is_le, 0);
PM_METAL_REG_REF(util_endian, load_u16_le, 1);
PM_METAL_REG_REF(util_endian, store_u16_le, 2);
PM_METAL_REG_REF(util_endian, load_u32_le, 3);
PM_METAL_REG_REF(util_endian, store_u32_le, 4);
PM_METAL_REG_REF(util_endian, load_u64_le, 5);
PM_METAL_REG_REF(util_endian, store_u64_le, 6);
PM_METAL_REG_MOD(util_endian, "pymergetic.metal.util.endian")

static int32_t util_endian_register_symbols(void *ctx)
{
  (void)ctx;
  pm_metal_reg_export_publish(util_endian_host_is_le, (void *)pm_metal_util_endian_host_is_le);
  pm_metal_reg_export_publish(util_endian_load_u16_le, (void *)pm_metal_util_endian_load_u16_le);
  pm_metal_reg_export_publish(util_endian_store_u16_le, (void *)pm_metal_util_endian_store_u16_le);
  pm_metal_reg_export_publish(util_endian_load_u32_le, (void *)pm_metal_util_endian_load_u32_le);
  pm_metal_reg_export_publish(util_endian_store_u32_le, (void *)pm_metal_util_endian_store_u32_le);
  pm_metal_reg_export_publish(util_endian_load_u64_le, (void *)pm_metal_util_endian_load_u64_le);
  pm_metal_reg_export_publish(util_endian_store_u64_le, (void *)pm_metal_util_endian_store_u64_le);
  return 0;
}

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
