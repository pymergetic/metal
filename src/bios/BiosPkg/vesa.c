/** @file
  VESA LFB detector via BIOS INT 10. (impl: bios)
  Same inventory model as Bochs/Multiboot — not an arch fork.
**/
#include "fb_detect.h"
#include "vesa_int10.h"

#include <pymergetic/metal/boot/port.h>

#include "../shim/PmBiosUefi.h"
#include "../shim/Library/IoLib.h"

/* Low-RAM scratch (below Multiboot image at 1MiB). */
#define VBE_INFO_ADDR 0x9000u
#define VBE_MODE_ADDR 0x9200u

typedef struct {
  CHAR8 Signature[4];
  UINT16 Version;
  UINT32 OemStringPtr;
  UINT32 Capabilities;
  UINT32 VideoModePtr;
  UINT16 TotalMemory;
  UINT16 OemSoftwareRev;
  UINT32 OemVendorNamePtr;
  UINT32 OemProductNamePtr;
  UINT32 OemProductRevPtr;
  UINT8 Reserved[222];
  UINT8 OemData[256];
} __attribute__((packed)) vbe_info_t;

typedef struct {
  UINT16 ModeAttributes;
  UINT8 WinAAttributes;
  UINT8 WinBAttributes;
  UINT16 WinGranularity;
  UINT16 WinSize;
  UINT16 WinASegment;
  UINT16 WinBSegment;
  UINT32 WinFuncPtr;
  UINT16 BytesPerScanLine;
  UINT16 XResolution;
  UINT16 YResolution;
  UINT8 XCharSize;
  UINT8 YCharSize;
  UINT8 NumberOfPlanes;
  UINT8 BitsPerPixel;
  UINT8 NumberOfBanks;
  UINT8 MemoryModel;
  UINT8 BankSize;
  UINT8 NumberOfImagePages;
  UINT8 Reserved0;
  UINT8 RedMaskSize;
  UINT8 RedFieldPosition;
  UINT8 GreenMaskSize;
  UINT8 GreenFieldPosition;
  UINT8 BlueMaskSize;
  UINT8 BlueFieldPosition;
  UINT8 RsvdMaskSize;
  UINT8 RsvdFieldPosition;
  UINT8 DirectColorModeInfo;
  UINT32 PhysBasePtr;
  UINT32 Reserved1;
  UINT16 Reserved2;
  UINT16 LinBytesPerScanLine;
} __attribute__((packed)) vbe_mode_t;

STATIC VOID
VesaCom1Puts(CONST CHAR8 *s)
{
  while (s && *s) {
    UINTN spins;
    for (spins = 0; spins < 100000; spins++) {
      if ((IoRead8(0x3FD) & 0x20) != 0)
	break;
    }
    IoWrite8(0x3F8, (UINT8) *s++);
  }
}

STATIC VOID *
FarToPtr(UINT32 far_ptr)
{
  UINT16 off = (UINT16)(far_ptr & 0xffffu);
  UINT16 seg = (UINT16)(far_ptr >> 16);
  return (VOID *)(UINTN)(((UINT32)seg << 4) + (UINT32)off);
}

int
pm_bios_vesa_int10(UINT16 *ax, UINT16 *bx, UINT16 *cx, UINT16 *dx, UINT16 *es,
		   UINT16 *di)
{
  return pm_bios_rm_int(0x10, ax, bx, cx, dx, es, di, NULL);
}

STATIC INT32
VbeCall(UINT16 ax_in, UINT16 bx_in, UINT16 cx_in, UINT16 dx_in, UINT16 es_in,
	UINT16 di_in, UINT16 *ax_out)
{
  UINT16 ax = ax_in;
  UINT16 bx = bx_in;
  UINT16 cx = cx_in;
  UINT16 dx = dx_in;
  UINT16 es = es_in;
  UINT16 di = di_in;

  if (pm_bios_vesa_int10(&ax, &bx, &cx, &dx, &es, &di) != 0)
    return -1;
  if (ax_out)
    *ax_out = ax;
  if ((ax & 0x00ffu) != 0x4fu)
    return -1;
  if ((ax & 0xff00u) != 0)
    return -1;
  return 0;
}

STATIC INT32
ModeRank(UINT16 w, UINT16 h, UINT8 bpp)
{
  if (bpp != 32)
    return -1;
  if (w == 1024 && h == 768)
    return 30;
  if (w == 800 && h == 600)
    return 20;
  if (w == 640 && h == 480)
    return 10;
  if (w >= 640 && h >= 480)
    return 1;
  return -1;
}

int
pm_bios_fb_vesa_detect(VOID)
{
  vbe_info_t *info = (vbe_info_t *)(UINTN)VBE_INFO_ADDR;
  vbe_mode_t *mode = (vbe_mode_t *)(UINTN)VBE_MODE_ADDR;
  UINT16 *modes;
  UINT16 ax;
  UINT16 best_mode = 0xffffu;
  INT32 best_rank = -1;
  UINT16 best_w = 0;
  UINT16 best_h = 0;
  UINT32 best_pitch = 0;
  UINT32 best_fb = 0;
  UINTN i;
  UINT32 ppsl;
  VOID *fb;
  unsigned w, h, cur_ppsl;

  if (pm_metal_port_get_framebuffer(&fb, &w, &h, &cur_ppsl) == 0)
    return 0;

  for (i = 0; i < sizeof(*info); i++)
    ((UINT8 *)info)[i] = 0;
  info->Signature[0] = 'V';
  info->Signature[1] = 'B';
  info->Signature[2] = 'E';
  info->Signature[3] = '2';

  if (VbeCall(0x4f00, 0, 0, 0, (UINT16)(VBE_INFO_ADDR >> 4), 0, &ax) != 0)
    return -1;
  if (info->Signature[0] != 'V' || info->Signature[1] != 'E'
      || info->Signature[2] != 'S' || info->Signature[3] != 'A')
    return -1;
  if (info->VideoModePtr == 0)
    return -1;

  modes = (UINT16 *)FarToPtr(info->VideoModePtr);
  for (i = 0; i < 512; i++) {
    UINT16 m = modes[i];
    INT32 rank;
    UINT32 pitch;
    UINTN z;

    if (m == 0xffffu)
      break;

    for (z = 0; z < sizeof(*mode); z++)
      ((UINT8 *)mode)[z] = 0;

    if (VbeCall(0x4f01, 0, m, 0, (UINT16)(VBE_MODE_ADDR >> 4), 0, &ax) != 0)
      continue;

    if ((mode->ModeAttributes & 0x81u) != 0x81u)
      continue;
    if (mode->PhysBasePtr == 0)
      continue;
    if (mode->BitsPerPixel != 32)
      continue;

    rank = ModeRank(mode->XResolution, mode->YResolution, mode->BitsPerPixel);
    if (rank < 0 || rank < best_rank)
      continue;

    pitch = mode->BytesPerScanLine;
    if (info->Version >= 0x300 && mode->LinBytesPerScanLine != 0)
      pitch = mode->LinBytesPerScanLine;
    if (pitch < (UINT32)mode->XResolution * 4u)
      pitch = (UINT32)mode->XResolution * 4u;

    best_rank = rank;
    best_mode = m;
    best_w = mode->XResolution;
    best_h = mode->YResolution;
    best_pitch = pitch;
    best_fb = mode->PhysBasePtr;

    if (rank >= 30)
      break;
  }

  if (best_rank < 0 || best_mode == 0xffffu || best_fb == 0)
    return -1;

  if (VbeCall(0x4f02, (UINT16)(best_mode | 0x4000u), 0, 0, 0, 0, &ax) != 0)
    return -1;

  /* Re-query after set — some BIOSes fix PhysBasePtr / pitch only then. */
  {
    UINTN z;
    for (z = 0; z < sizeof(*mode); z++)
      ((UINT8 *)mode)[z] = 0;
  }
  if (VbeCall(0x4f01, 0, best_mode, 0, (UINT16)(VBE_MODE_ADDR >> 4), 0, &ax)
      == 0 && mode->PhysBasePtr != 0) {
    best_fb = mode->PhysBasePtr;
    best_w = mode->XResolution;
    best_h = mode->YResolution;
    best_pitch = mode->BytesPerScanLine;
    if (info->Version >= 0x300 && mode->LinBytesPerScanLine != 0)
      best_pitch = mode->LinBytesPerScanLine;
    if (best_pitch < (UINT32)best_w * 4u)
      best_pitch = (UINT32)best_w * 4u;
  }

  ppsl = best_pitch / 4u;
  if (ppsl == 0)
    ppsl = best_w;
  pm_metal_port_set_framebuffer((VOID *)(UINTN)best_fb, best_w, best_h, ppsl);

  /* Text mode is gone — paint LFB so the panel is not stuck black. */
  {
    UINT32 *fb = (UINT32 *)(UINTN)best_fb;
    UINT32 y, x;
    for (y = 0; y < best_h; y++) {
      UINT32 *row = fb + (UINTN)y * (UINTN)ppsl;
      UINT32 color = (y < 32u) ? 0xFF2E86ABu : 0xFF1A1A2Eu;
      for (x = 0; x < best_w; x++)
	row[x] = color;
    }
  }

  VesaCom1Puts("metal-bios: fb/vesa ");
  if (best_w == 1024)
    VesaCom1Puts("1024x768x32\r\n");
  else if (best_w == 800)
    VesaCom1Puts("800x600x32\r\n");
  else if (best_w == 640)
    VesaCom1Puts("640x480x32\r\n");
  else
    VesaCom1Puts("lfb\r\n");

  return 0;
}
