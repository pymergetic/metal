/** @file
  BIOS gfx harvest — Multiboot/VESA LFB via port framebuffer.
**/
#include <stdint.h>
#include <pymergetic/metal/boot/port.h>

#include <Uefi.h>

int
pm_metal_gfx_harvest_port (
  UINT32  **fb,
  UINT32   *width,
  UINT32   *height,
  UINT32   *ppsl,
  VOID    **gop
  )
{
  VOID      *raw;
  unsigned   w;
  unsigned   h;
  unsigned   p;

  if (fb == NULL || width == NULL || height == NULL || ppsl == NULL || gop == NULL) {
    return -1;
  }

  if (pm_metal_port_get_framebuffer (&raw, &w, &h, &p) != 0) {
    return -1;
  }

  *fb     = (UINT32 *)raw;
  *width  = (UINT32)w;
  *height = (UINT32)h;
  *ppsl   = (UINT32)(p ? p : w);
  *gop    = NULL;
  return 0;
}
