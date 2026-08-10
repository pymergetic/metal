/*
 * Halt / reset — platform OWN (bios/efi power.c).
 *
 * reboot == 0 → power off (ACPI S5 / EFI ResetShutdown)
 * reboot != 0 → restart (KBC / EFI ResetCold)
 */
#ifndef PYMERGETIC_METAL_BOOT_POWER_H_
#define PYMERGETIC_METAL_BOOT_POWER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_boot_power_ops {
    void (*halt)(void);
    void (*reset)(int32_t reboot);
} pm_metal_boot_power_ops_t;

const pm_metal_boot_power_ops_t *pm_metal_boot_power_ops(void);

/** Never returns. */
void pm_metal_boot_halt(void);

/** Never returns on success. reboot: 0=off, nonzero=restart. */
void pm_metal_boot_reset(int32_t reboot);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_POWER_H_ */
