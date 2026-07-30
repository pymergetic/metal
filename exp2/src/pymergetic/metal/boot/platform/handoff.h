/*
 * Leave firmware services / enter owned floor (host-only).
 *
 * Port contract (platform/). Bodies: boot/bios|efi/handoff.c
 * Public root header: GENERATED from Rust later.
 */
#ifndef PYMERGETIC_METAL_BOOT_HANDOFF_H_
#define PYMERGETIC_METAL_BOOT_HANDOFF_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_boot_handoff_ops {
  /**
   * Drop firmware boot services / enter owned runtime.
   * Returns 0 on success paths that return; negative on failure.
   * May not return on some targets.
   */
  int32_t (*leave_firmware)(void);
} pm_metal_boot_handoff_ops_t;

const pm_metal_boot_handoff_ops_t *pm_metal_boot_handoff_ops(void);

static inline int32_t pm_metal_boot_leave_firmware(void)
{
  return pm_metal_boot_handoff_ops()->leave_firmware();
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_HANDOFF_H_ */
