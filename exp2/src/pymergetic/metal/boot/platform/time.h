/*
 * Early TSC calibrate delay (host-only). Shared time cache lives in async.
 *
 * Bodies: boot/platform/bios/time.c, boot/platform/efi/time.c
 */
#ifndef PYMERGETIC_METAL_BOOT_TIME_H_
#define PYMERGETIC_METAL_BOOT_TIME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_boot_time_ops {
  /**
   * Measure TSC ticks per microsecond (multi-sample).
   * Returns 0 on failure. May be called before leave_firmware.
   */
  uint64_t (*tsc_per_us)(void);
  /** Drop any sticky port cache so the next tsc_per_us remeasures. */
  void (*invalidate)(void);
} pm_metal_boot_time_ops_t;

const pm_metal_boot_time_ops_t *pm_metal_boot_time_ops(void);

static inline uint64_t pm_metal_boot_time_tsc_per_us(void)
{
  return pm_metal_boot_time_ops()->tsc_per_us();
}

static inline void pm_metal_boot_time_invalidate(void)
{
  pm_metal_boot_time_ops()->invalidate();
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_TIME_H_ */
