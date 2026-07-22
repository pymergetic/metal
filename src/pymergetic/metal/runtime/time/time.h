/** @file
  Cooperative time — stall helpers for tests / delayed wake planning.
**/
#ifndef PM_METAL_RUNTIME_TIME_TIME_H_
#define PM_METAL_RUNTIME_TIME_TIME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Calibrate TSC (BSP, before APs). Idempotent. */
/* impl: efi|bios */
void pm_metal_time_init (void);

/** Force a fresh TSC calibration (post-ExitBootServices). */
/* impl: efi|bios */
void pm_metal_time_recalibrate (void);

/** Busy-stall at least `us` microseconds (TSC; MP-safe). */
/* impl: efi|bios */
void pm_metal_time_usleep (uint32_t us);

/** Busy-stall at least `ms` milliseconds. */
/* impl: efi|bios */
void pm_metal_time_msleep (uint32_t ms);

/** Busy-stall at least `sec` seconds. */
/* impl: efi|bios */
void pm_metal_time_sleep (uint32_t sec);

/** Monotonic microseconds from TSC (MP-safe). */
/* impl: efi|bios */
uint64_t pm_metal_time_mono_us (void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_RUNTIME_TIME_TIME_H_ */
