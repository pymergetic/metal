/*
 * Firmware memory map for arena claim (host-only).
 *
 * impl: bios - boot/platform/bios/mem_map.c
 * impl: efi  - boot/platform/efi/mem_map.c
 */
#ifndef PYMERGETIC_METAL_BOOT_MEM_MAP_H_
#define PYMERGETIC_METAL_BOOT_MEM_MAP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/** Region kinds after platform normalization. */
typedef enum pm_metal_boot_mem_type {
  PM_METAL_BOOT_MEM_AVAILABLE = 1,
  PM_METAL_BOOT_MEM_RESERVED = 2,
  PM_METAL_BOOT_MEM_ACPI_RECLAIM = 3,
  PM_METAL_BOOT_MEM_ACPI_NVS = 4,
  PM_METAL_BOOT_MEM_OTHER = 5
} pm_metal_boot_mem_type_t;

typedef struct pm_metal_boot_mem_region {
  uint64_t addr;
  uint64_t len;
  uint32_t type; /* pm_metal_boot_mem_type_t */
  uint32_t reserved;
} pm_metal_boot_mem_region_t;

typedef struct pm_metal_boot_mem_map_ops {
  /**
   * Fill out[0..*n_out) with up to max regions.
   * Returns 0 on success, negative on failure.
   */
  int32_t (*get)(pm_metal_boot_mem_region_t *out, uint32_t max, uint32_t *n_out);
  /** Load address of the Metal image (0 if unknown). */
  uintptr_t (*image_base)(void);
  /** First byte past the loaded image (linker floor). */
  uintptr_t (*image_end)(void);
} pm_metal_boot_mem_map_ops_t;

const pm_metal_boot_mem_map_ops_t *pm_metal_boot_mem_map_ops(void);

static inline int32_t pm_metal_boot_mem_map_get(pm_metal_boot_mem_region_t *out,
                                               uint32_t max,
                                               uint32_t *n_out)
{
  return pm_metal_boot_mem_map_ops()->get(out, max, n_out);
}

static inline uintptr_t pm_metal_boot_mem_map_image_base(void)
{
  return pm_metal_boot_mem_map_ops()->image_base();
}

static inline uintptr_t pm_metal_boot_mem_map_image_end(void)
{
  return pm_metal_boot_mem_map_ops()->image_end();
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_MEM_MAP_H_ */
