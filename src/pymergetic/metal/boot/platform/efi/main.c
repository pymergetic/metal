#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/main.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <Uefi.h>

#include <pymergetic/metal/boot/platform/gop.h>
#include <pymergetic/metal/boot/platform/power.h>
#include <pymergetic/metal/boot/platform/private/bringup.h>

#include "efi_ctx.h"

void pm_metal_efi_acpi_seed(void);

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
  pm_metal_efi_ctx_set(image, st);
  pm_metal_efi_acpi_seed();
  /* Capture GOP FB/protocol before ExitBootServices (bringup leaves firmware). */
  pm_metal_boot_efi_gop_stash();
  if (pm_metal_boot_bringup() != 0) {
    pm_metal_boot_halt();
  }
  pm_metal_boot_halt();
  return EFI_SUCCESS;
}
