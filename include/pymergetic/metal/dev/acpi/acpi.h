/*
 * Minimal ACPI — soft-off (S5) via FADT PM1 + DSDT \_S5_.
 *
 * impl: common — src/pymergetic/metal/dev/acpi/acpi_power.c
 */
#ifndef PYMERGETIC_METAL_DEV_ACPI_ACPI_H_
#define PYMERGETIC_METAL_DEV_ACPI_ACPI_H_

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/**
 * Best-effort ACPI power off. Returns only if tables are missing/malformed
 * or the write did not take effect.
 */
void pm_metal_acpi_poweroff(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_ACPI_ACPI_H_ */
