/* pymergetic.metal.boot — one bring-up. Platforms fill HW; this runs cards. */
#ifndef PYMERGETIC_METAL_BOOT_TYPES_H
#define PYMERGETIC_METAL_BOOT_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

int pm_metal_boot(void);
int pm_metal_ready(void);
pm_util_mem_arena_t *pm_metal_boot_arena(void);
int32_t pm_metal_boot_feed_span(uint64_t base, uint64_t len);

/* Platform fills (weak defaults in __impl__.c). */
int pm_metal_boot_fill_hosted_span(void **base, size_t *len);
void pm_metal_boot_fill_release(void *base, size_t len);
void pm_metal_boot_fill_avoid(uint64_t *lo, uint64_t *hi);
size_t pm_metal_boot_fill_arena_need(void);
void pm_metal_boot_fill_io(void);
void pm_metal_boot_fill_bind_arena(pm_util_mem_arena_t *arena);
const char *pm_metal_boot_fill_seat(void);
int pm_metal_boot_fill_kernel(uint64_t *base, uint64_t *len);
const char *pm_metal_boot_fill_map_label(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_TYPES_H */
