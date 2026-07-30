#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/handoff.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <Uefi.h>

#include <pymergetic/metal/boot/platform/handoff.h>

#include "efi_ctx.h"

static int32_t efi_leave_firmware(void)
{
  EFI_STATUS st;
  UINTN map_size;
  UINTN map_key;
  UINTN desc_size;
  UINT32 desc_ver;
  EFI_MEMORY_DESCRIPTOR *map;
  UINTN tries;

  if (!g_pm_efi_bs_alive || g_pm_efi_st == NULL || g_pm_efi_st->BootServices == NULL) {
    return 0;
  }

  for (tries = 0; tries < 8u; tries++) {
    map_size = 0;
    st = g_pm_efi_st->BootServices->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
    if (st != EFI_BUFFER_TOO_SMALL || map_size == 0) {
      return -1;
    }
    map_size += 4u * desc_size;
    st = g_pm_efi_st->BootServices->AllocatePool(EfiLoaderData, map_size, (VOID **)&map);
    if (EFI_ERROR(st) || map == NULL) {
      return -1;
    }
    st = g_pm_efi_st->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(st)) {
      (void)g_pm_efi_st->BootServices->FreePool(map);
      return -1;
    }
    st = g_pm_efi_st->BootServices->ExitBootServices(g_pm_efi_image, map_key);
    if (!EFI_ERROR(st)) {
      pm_metal_efi_ctx_bs_dead();
      /* Map buffer is gone with BS — do not FreePool. */
      return 0;
    }
    (void)g_pm_efi_st->BootServices->FreePool(map);
  }
  return -1;
}

static const pm_metal_boot_handoff_ops_t g_ops = {
  .leave_firmware = efi_leave_firmware,
};

const pm_metal_boot_handoff_ops_t *pm_metal_boot_handoff_ops(void)
{
  return &g_ops;
}
