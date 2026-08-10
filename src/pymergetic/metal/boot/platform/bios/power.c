#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/power.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/power.h>
#include <pymergetic/metal/dev/acpi/__init__.h>

#include "io.h"

static void bios_halt(void)
{
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static void bios_kbc_restart(void)
{
    uint32_t spins;

    for (spins = 0; spins < 100000u; spins++) {
        if ((pm_metal_bios_inb(0x64u) & 0x02u) == 0u) {
            break;
        }
    }
    pm_metal_bios_outb(0x64u, 0xFEu);
    bios_halt();
}

static void bios_power_off(void)
{
    /* Real ACPI S5 — works on hardware and on QEMU's ACPI chipset. */
    (void)pm_metal_dev_acpi_power_off();
    bios_halt();
}

static void bios_reset(int32_t reboot)
{
    if (reboot == 0) {
        bios_power_off();
    }
    bios_kbc_restart();
}

void pm_metal_boot_halt(void)
{
    bios_halt();
}

void pm_metal_boot_reset(int32_t reboot)
{
    bios_reset(reboot);
}

static const pm_metal_boot_power_ops_t g_ops = {
    .halt = bios_halt,
    .reset = bios_reset,
};

const pm_metal_boot_power_ops_t *pm_metal_boot_power_ops(void)
{
    return &g_ops;
}
