#include "pci.h"
#include <Library/IoLib.h>
#include <IndustryStandard/Pci.h>

/* EDK2 uses EFI_PCI_* names; BIOS shim keeps PCI_*. */
#ifndef PCI_COMMAND_MEMORY_SPACE
#define PCI_COMMAND_MEMORY_SPACE  EFI_PCI_COMMAND_MEMORY_SPACE
#endif
#ifndef PCI_COMMAND_BUS_MASTER
#define PCI_COMMAND_BUS_MASTER    EFI_PCI_COMMAND_BUS_MASTER
#endif
#ifndef PCI_COMMAND_IO_SPACE
#define PCI_COMMAND_IO_SPACE      0x0001
#endif
#ifndef PCI_BAR0
#define PCI_BAR0                  0x10
#endif
#ifndef PCI_COMMAND_OFFSET
#define PCI_COMMAND_OFFSET        0x04
#endif
#ifndef PCI_REVISION_ID_OFFSET
#define PCI_REVISION_ID_OFFSET    0x08
#endif
#ifndef PCI_CLASSCODE_OFFSET
#define PCI_CLASSCODE_OFFSET      0x09
#endif
#ifndef PCI_HEADER_TYPE_OFFSET
#define PCI_HEADER_TYPE_OFFSET    0x0E
#endif
#ifndef HEADER_TYPE_MULTI_FUNCTION
#define HEADER_TYPE_MULTI_FUNCTION 0x80
#endif
#ifndef PCI_VENDOR_ID_OFFSET
#define PCI_VENDOR_ID_OFFSET      0x00
#endif
#ifndef PCI_DEVICE_ID_OFFSET
#define PCI_DEVICE_ID_OFFSET      0x02
#endif

static UINT32
Addr(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset)
{
  return 0x80000000u | ((UINT32)bus << 16) | ((UINT32)(dev & 0x1f) << 11) |
	 ((UINT32)(func & 0x7) << 8) | (UINT32)(offset & 0xfc);
}

UINT32
pm_bios_pci_read32(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset)
{
  IoWrite32(0xCF8, Addr(bus, dev, func, offset));
  return IoRead32(0xCFC);
}

UINT16
pm_bios_pci_read16(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset)
{
  UINT32 v = pm_bios_pci_read32(bus, dev, func, offset);
  return (UINT16)((v >> ((offset & 2) * 8)) & 0xffffu);
}

UINT8
pm_bios_pci_read8(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset)
{
  UINT32 v = pm_bios_pci_read32(bus, dev, func, offset);
  return (UINT8)((v >> ((offset & 3) * 8)) & 0xffu);
}

VOID
pm_bios_pci_write32(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset, UINT32 val)
{
  IoWrite32(0xCF8, Addr(bus, dev, func, offset));
  IoWrite32(0xCFC, val);
}

VOID
pm_bios_pci_write16(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset, UINT16 val)
{
  UINT32 cur = pm_bios_pci_read32(bus, dev, func, offset);
  UINT32 shift = (offset & 2) * 8;
  cur = (cur & ~(0xffffu << shift)) | ((UINT32)val << shift);
  pm_bios_pci_write32(bus, dev, func, offset, cur);
}

VOID
pm_bios_pci_enable_mem_bm(UINT8 bus, UINT8 dev, UINT8 func)
{
  UINT16 cmd = pm_bios_pci_read16(bus, dev, func, PCI_COMMAND_OFFSET);
  cmd |= (UINT16)(PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);
  pm_bios_pci_write16(bus, dev, func, PCI_COMMAND_OFFSET, cmd);
}

VOID
pm_bios_pci_enable_io_bm(UINT8 bus, UINT8 dev, UINT8 func)
{
  UINT16 cmd = pm_bios_pci_read16(bus, dev, func, PCI_COMMAND_OFFSET);
  cmd |= (UINT16)(PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER);
  pm_bios_pci_write16(bus, dev, func, PCI_COMMAND_OFFSET, cmd);
}

UINT64
pm_bios_pci_bar_mmio(UINT8 bus, UINT8 dev, UINT8 func, UINT8 bar_index,
		     UINT8 *bars_consumed)
{
  UINT8 off = (UINT8)(PCI_BAR0 + bar_index * 4);
  UINT32 lo = pm_bios_pci_read32(bus, dev, func, off);
  UINT64 base;

  if (bars_consumed != NULL)
    *bars_consumed = 1;
  if ((lo & 1u) != 0)
    return 0; /* I/O */
  base = (UINT64)(lo & ~0xfu);
  if (((lo >> 1) & 3u) == 2u) {
    UINT32 hi = pm_bios_pci_read32(bus, dev, func, (UINT8)(off + 4));
    base |= (UINT64)hi << 32;
    if (bars_consumed != NULL)
      *bars_consumed = 2;
  }
  return base;
}

UINT16
pm_bios_pci_bar_io(UINT8 bus, UINT8 dev, UINT8 func, UINT8 bar_index)
{
  UINT8 off = (UINT8)(PCI_BAR0 + bar_index * 4);
  UINT32 bar = pm_bios_pci_read32(bus, dev, func, off);

  if ((bar & 1u) == 0)
    return 0;
  return (UINT16)(bar & ~0x3u);
}

int
pm_bios_pci_find (
  UINT16  vendor,
  UINT16  device,
  UINT8  *bus_out,
  UINT8  *dev_out,
  UINT8  *func_out
  )
{
  UINT16  bus;
  UINT8   dev;
  UINT8   func;
  UINT8   hdr;
  UINT8   fmax;

  for (bus = 0; bus < 256; bus++) {
    for (dev = 0; dev < 32; dev++) {
      UINT16  ven;

      ven = pm_bios_pci_read16 ((UINT8)bus, dev, 0, PCI_VENDOR_ID_OFFSET);
      if (ven == 0xffff) {
        continue;
      }

      hdr  = pm_bios_pci_read8 ((UINT8)bus, dev, 0, PCI_HEADER_TYPE_OFFSET);
      fmax = (hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (func = 0; func < fmax; func++) {
        ven = pm_bios_pci_read16 ((UINT8)bus, dev, func, PCI_VENDOR_ID_OFFSET);
        if (ven != vendor) {
          continue;
        }

        if (pm_bios_pci_read16 ((UINT8)bus, dev, func, PCI_DEVICE_ID_OFFSET)
            == device)
        {
          if (bus_out != NULL) {
            *bus_out = (UINT8)bus;
          }

          if (dev_out != NULL) {
            *dev_out = dev;
          }

          if (func_out != NULL) {
            *func_out = func;
          }

          return 0;
        }
      }
    }
  }

  return -1;
}

int
pm_bios_pci_find_class (
  UINT8   base_class,
  UINT8   subclass,
  UINT8  *bus_out,
  UINT8  *dev_out,
  UINT8  *func_out
  )
{
  UINT16  bus;
  UINT8   dev;
  UINT8   func;
  UINT8   hdr;
  UINT8   fmax;

  for (bus = 0; bus < 256; bus++) {
    for (dev = 0; dev < 32; dev++) {
      UINT16  ven;

      ven = pm_bios_pci_read16 ((UINT8)bus, dev, 0, PCI_VENDOR_ID_OFFSET);
      if (ven == 0xffff) {
        continue;
      }

      hdr  = pm_bios_pci_read8 ((UINT8)bus, dev, 0, PCI_HEADER_TYPE_OFFSET);
      fmax = (hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (func = 0; func < fmax; func++) {
        UINT32  id;
        UINT8   cls;
        UINT8   sub;

        ven = pm_bios_pci_read16 ((UINT8)bus, dev, func, PCI_VENDOR_ID_OFFSET);
        if (ven == 0xffff) {
          continue;
        }

        /* dword @ 0x08: rev | progIF | subclass | base_class */
        id  = pm_bios_pci_read32 (
                (UINT8)bus,
                dev,
                func,
                PCI_REVISION_ID_OFFSET
                );
        cls = (UINT8)((id >> 24) & 0xffu);
        sub = (UINT8)((id >> 16) & 0xffu);
        if (cls != base_class || sub != subclass) {
          continue;
        }

        if (bus_out != NULL) {
          *bus_out = (UINT8)bus;
        }

        if (dev_out != NULL) {
          *dev_out = dev;
        }

        if (func_out != NULL) {
          *func_out = func;
        }

        return 0;
      }
    }
  }

  return -1;
}
