/*
 * Halt / reset (host-only).
 *
 * impl: bios - boot/platform/bios/power.c
 * impl: efi  - boot/platform/efi/power.c
 */
#ifndef PYMERGETIC_METAL_BOOT_POWER_H_
#define PYMERGETIC_METAL_BOOT_POWER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_boot_power_ops {
  /** Stop the CPU. May not return. */
  void (*halt)(void);
  /**
   * reboot == 0: power off if possible; non-zero: restart.
   * Does not return.
   */
  void (*reset)(int32_t reboot);
} pm_metal_boot_power_ops_t;

const pm_metal_boot_power_ops_t *pm_metal_boot_power_ops(void);

static inline void pm_metal_boot_halt(void)
{
  pm_metal_boot_power_ops()->halt();
}

static inline void pm_metal_boot_reset(int32_t reboot)
{
  pm_metal_boot_power_ops()->reset(reboot);
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_POWER_H_ */
