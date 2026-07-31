#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/efi_ctx.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include "efi_ctx.h"

EFI_HANDLE g_pm_efi_image;
EFI_SYSTEM_TABLE *g_pm_efi_st;
int g_pm_efi_bs_alive;

void pm_metal_efi_ctx_set(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
  g_pm_efi_image = image;
  g_pm_efi_st = st;
  g_pm_efi_bs_alive = (st != NULL && st->BootServices != NULL) ? 1 : 0;
}

void pm_metal_efi_ctx_bs_dead(void)
{
  g_pm_efi_bs_alive = 0;
}
