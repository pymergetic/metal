/* pymergetic.metal.fw.memmap — memory ranges published into metal.dt. */
#ifndef PYMERGETIC_METAL_FW_MEMMAP_TYPES_H
#define PYMERGETIC_METAL_FW_MEMMAP_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_fw_memmap_feed_mmap(const void *mmap, uint32_t bytes);
int32_t pm_metal_fw_memmap_feed_efi(const void *map, uint32_t desc_size, uint32_t bytes);
int32_t pm_metal_fw_memmap_load_efi(const void *map, uint32_t desc_size, uint32_t bytes);
int32_t pm_metal_fw_memmap_pick(uint64_t avoid_lo, uint64_t avoid_hi, uint64_t want,
    uint64_t *out_base, uint64_t *out_len);
int32_t pm_metal_fw_memmap_add_spare(pm_util_mem_arena_t *arena, uint64_t avoid_lo, uint64_t avoid_hi,
    uint64_t used_lo, uint64_t used_hi);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_FW_MEMMAP_TYPES_H */
