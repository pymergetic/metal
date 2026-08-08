#ifndef PM_METAL_DEV_ACPI_H_
#define PM_METAL_DEV_ACPI_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Seed RSDP (EFI config table) before BIOS low-mem scan. */
void pm_metal_dev_acpi_set_rsdp(uint64_t addr);
uint64_t pm_metal_dev_acpi_rsdp(void);

/* Find RSDP if unset (BIOS EBDA + 0xE0000), parse MADT. */
int32_t pm_metal_dev_acpi_init(void);

/* Enabled Local APIC / x2APIC processors from MADT (0 if unknown). */
uint32_t pm_metal_dev_acpi_cpu_count(void);

/* APIC id for logical CPU index [0, cpu_count). BSP is index 0 when possible. */
uint32_t pm_metal_dev_acpi_apic_id(uint32_t cpu_index);

/* MADT Local APIC MMIO base (default 0xFEE00000). */
uint64_t pm_metal_dev_acpi_lapic_base(void);

#ifdef __cplusplus
}
#endif

#endif
