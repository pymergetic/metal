#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/power.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <Uefi.h>

#include <pymergetic/metal/boot/platform/power.h>

#include "efi_ctx.h"

static void efi_halt(void)
{
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static void efi_reset(int32_t reboot)
{
    if (g_pm_efi_st != NULL && g_pm_efi_st->RuntimeServices != NULL) {
        (void)g_pm_efi_st->RuntimeServices->ResetSystem(
            reboot ? EfiResetCold : EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    }
    efi_halt();
}

void pm_metal_boot_halt(void)
{
    efi_halt();
}

void pm_metal_boot_reset(int32_t reboot)
{
    efi_reset(reboot);
}

static const pm_metal_boot_power_ops_t g_ops = {
    .halt = efi_halt,
    .reset = efi_reset,
};

const pm_metal_boot_power_ops_t *pm_metal_boot_power_ops(void)
{
    return &g_ops;
}
