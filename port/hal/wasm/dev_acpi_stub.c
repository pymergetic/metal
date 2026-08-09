/*
 * Browser dev.acpi — same C ABI; single logical CPU, default LAPIC base.
 */
#include "pymergetic/metal/dev/acpi/__init__.h"

static uint64_t g_rsdp;

void pm_metal_dev_acpi_set_rsdp(uint64_t addr) { g_rsdp = addr; }

uint64_t pm_metal_dev_acpi_rsdp(void) { return g_rsdp; }

int32_t pm_metal_dev_acpi_init(void) { return 0; }

uint32_t pm_metal_dev_acpi_cpu_count(void) { return 1u; }

uint32_t pm_metal_dev_acpi_apic_id(uint32_t cpu_index)
{
    (void)cpu_index;
    return 0u;
}

uint64_t pm_metal_dev_acpi_lapic_base(void) { return 0xFEE00000ull; }
