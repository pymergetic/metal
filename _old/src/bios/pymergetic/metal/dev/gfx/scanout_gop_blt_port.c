/** @file
  BIOS body for the one EDK2 primitive scanout_gop_blt.c needs. No real
  UEFI GOP protocol exists under Multiboot2/BIOS — always fails. Dead code
  in practice: this scanout backend's Probe() never registers under BIOS
  because pm_metal_scanout_bind_t::gop is never set there.
**/

#include <stdint.h>

int pm_metal_gop_port_blt(void          *gop,
                           const uint32_t *src,
                           uint32_t        src_x,
                           uint32_t        src_y,
                           uint32_t        dst_x,
                           uint32_t        dst_y,
                           uint32_t        w,
                           uint32_t        h,
                           uint32_t        delta)
{
  (void)gop;
  (void)src;
  (void)src_x;
  (void)src_y;
  (void)dst_x;
  (void)dst_y;
  (void)w;
  (void)h;
  (void)delta;
  return -1;
}
