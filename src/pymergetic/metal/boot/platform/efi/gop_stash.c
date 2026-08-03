#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/gop_stash.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

#include <pymergetic/metal/boot/platform/gop.h>

#include "efi_ctx.h"

/* EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID — avoid depending on EDK2 GUID .obj */
static EFI_GUID g_gop_guid = {
  0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
};

static uint32_t *s_fb;
static uint32_t s_w;
static uint32_t s_h;
static uint32_t s_ppsl;
static void *s_gop;
static int s_ok;

void pm_metal_boot_efi_gop_stash(void)
{
  EFI_STATUS st;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;

  s_ok = 0;
  s_fb = NULL;
  s_gop = NULL;
  s_w = 0;
  s_h = 0;
  s_ppsl = 0;

  if (!g_pm_efi_bs_alive || g_pm_efi_st == NULL || g_pm_efi_st->BootServices == NULL) {
    return;
  }

  st = g_pm_efi_st->BootServices->LocateProtocol(&g_gop_guid, NULL, (VOID **)&gop);
  if (EFI_ERROR(st) || gop == NULL || gop->Mode == NULL) {
    return;
  }
  info = gop->Mode->Info;
  if (info == NULL || gop->Mode->FrameBufferBase == 0) {
    return;
  }

  s_w = info->HorizontalResolution;
  s_h = info->VerticalResolution;
  s_ppsl = info->PixelsPerScanLine ? info->PixelsPerScanLine : info->HorizontalResolution;
  s_fb = (uint32_t *)(UINTN)gop->Mode->FrameBufferBase;
  s_gop = gop;
  s_ok = 1;
}

int32_t pm_metal_boot_efi_gop_stash_get(
    uint32_t **fb_out,
    uint32_t *w_out,
    uint32_t *h_out,
    uint32_t *ppsl_out,
    void **gop_out)
{
  if (!s_ok || fb_out == NULL || w_out == NULL || h_out == NULL || ppsl_out == NULL
      || gop_out == NULL) {
    return -1;
  }
  if (s_fb == NULL || s_w < 320u || s_h < 200u) {
    return -1;
  }
  *fb_out = s_fb;
  *w_out = s_w;
  *h_out = s_h;
  *ppsl_out = s_ppsl != 0u ? s_ppsl : s_w;
  /* Blt is only valid while Boot Services are alive. */
  *gop_out = g_pm_efi_bs_alive ? s_gop : NULL;
  return 0;
}
