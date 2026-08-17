/* x86 AP bring-up fill for pymergetic.metal.async (BIOS + UEFI). */
#ifndef PM_METAL_PORT_SMP_H
#define PM_METAL_PORT_SMP_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t pm_metal_async_fill_ncpu(void);
int32_t pm_metal_async_fill_start_aps(pm_util_mem_arena_t *arena, uint32_t ncpu,
    void (*entry)(void *));

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_PORT_SMP_H */
