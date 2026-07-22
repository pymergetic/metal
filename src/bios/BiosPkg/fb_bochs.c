/** @file
  Bochs/QEMU std VGA LFB detector. (impl: bios)
  One more FB backend — not a platform fork.
**/
#include "fb_detect.h"

#include <pymergetic/metal/boot/port.h>

#include "../shim/PmBiosUefi.h"
#include "../shim/Library/IoLib.h"
#include "bus/pci/pci.h"

int
pm_bios_fb_bochs_detect(VOID)
{
  VOID *fb;
  unsigned w, h, ppsl;
  UINT8 bus, dev, func;
  UINT16 id;
  UINT64 bar;
  UINTN b, d, f;

  if (pm_metal_port_get_framebuffer(&fb, &w, &h, &ppsl) == 0)
    return 0;

  /* vendor 0x1234 device 0x1111, BAR0 = LFB */
  for (b = 0; b < 256; b++) {
    for (d = 0; d < 32; d++) {
      for (f = 0; f < 8; f++) {
	bus = (UINT8)b;
	dev = (UINT8)d;
	func = (UINT8)f;
	id = pm_bios_pci_read16(bus, dev, func, 0);
	if (id != 0x1234)
	  continue;
	if (pm_bios_pci_read16(bus, dev, func, 2) != 0x1111)
	  continue;
	bar = pm_bios_pci_bar_mmio(bus, dev, func, 0, NULL);
	if (bar == 0 || bar >= 0x100000000ull)
	  continue;
	pm_bios_pci_enable_mem_bm(bus, dev, func);
	IoWrite16(0x01CE, 0); /* INDEX_ID */
	IoWrite16(0x01CF, 0xB0C5);
	IoWrite16(0x01CE, 4); /* ENABLE */
	IoWrite16(0x01CF, 0); /* disable */
	IoWrite16(0x01CE, 1);
	IoWrite16(0x01CF, 1024);
	IoWrite16(0x01CE, 2);
	IoWrite16(0x01CF, 768);
	IoWrite16(0x01CE, 3);
	IoWrite16(0x01CF, 32);
	IoWrite16(0x01CE, 4);
	IoWrite16(0x01CF, 0x41); /* ENABLED | LFB */
	pm_metal_port_set_framebuffer((VOID *)(UINTN)bar, 1024, 768, 1024);
	return 0;
      }
    }
  }
  return -1;
}
