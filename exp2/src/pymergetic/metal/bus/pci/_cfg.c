#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/boot/platform/io.h>

#include "_cfg.h"

#define PCI_COMMAND_MEMORY_SPACE   0x0002u
#define PCI_COMMAND_BUS_MASTER     0x0004u
#define PCI_COMMAND_IO_SPACE       0x0001u
#define PCI_BAR0                   0x10u
#define PCI_COMMAND_OFFSET         0x04u
#define PCI_REVISION_ID_OFFSET     0x08u
#define PCI_HEADER_TYPE_OFFSET     0x0Eu
#define HEADER_TYPE_MULTI_FUNCTION 0x80u
#define PCI_VENDOR_ID_OFFSET       0x00u
#define PCI_DEVICE_ID_OFFSET       0x02u

static uint32_t Addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
  return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 0x1f) << 11) |
         ((uint32_t)(func & 0x7) << 8) | (uint32_t)(offset & 0xfc);
}

uint32_t pm_metal_bus_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
  pm_metal_boot_out32(0xCF8, Addr(bus, dev, func, offset));
  return pm_metal_boot_in32(0xCFC);
}

uint16_t pm_metal_bus_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
  uint32_t v = pm_metal_bus_pci_read32(bus, dev, func, offset);
  return (uint16_t)((v >> ((offset & 2) * 8)) & 0xffffu);
}

uint8_t pm_metal_bus_pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
  uint32_t v = pm_metal_bus_pci_read32(bus, dev, func, offset);
  return (uint8_t)((v >> ((offset & 3) * 8)) & 0xffu);
}

void pm_metal_bus_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val)
{
  pm_metal_boot_out32(0xCF8, Addr(bus, dev, func, offset));
  pm_metal_boot_out32(0xCFC, val);
}

void pm_metal_bus_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t val)
{
  uint32_t cur   = pm_metal_bus_pci_read32(bus, dev, func, offset);
  uint32_t shift = (offset & 2) * 8;
  cur            = (cur & ~(0xffffu << shift)) | ((uint32_t)val << shift);
  pm_metal_bus_pci_write32(bus, dev, func, offset, cur);
}

void pm_metal_bus_pci_enable_mem_bm(uint8_t bus, uint8_t dev, uint8_t func)
{
  uint16_t cmd = pm_metal_bus_pci_read16(bus, dev, func, PCI_COMMAND_OFFSET);
  cmd |= (uint16_t)(PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);
  pm_metal_bus_pci_write16(bus, dev, func, PCI_COMMAND_OFFSET, cmd);
}

void pm_metal_bus_pci_enable_io_bm(uint8_t bus, uint8_t dev, uint8_t func)
{
  uint16_t cmd = pm_metal_bus_pci_read16(bus, dev, func, PCI_COMMAND_OFFSET);
  cmd |= (uint16_t)(PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER);
  pm_metal_bus_pci_write16(bus, dev, func, PCI_COMMAND_OFFSET, cmd);
}

uint64_t pm_metal_bus_pci_bar_mmio(
  uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index, uint8_t *bars_consumed)
{
  uint8_t  off = (uint8_t)(PCI_BAR0 + bar_index * 4);
  uint32_t lo  = pm_metal_bus_pci_read32(bus, dev, func, off);
  uint64_t base;

  if (bars_consumed != NULL) {
    *bars_consumed = 1;
  }
  if ((lo & 1u) != 0) {
    return 0;
  }
  base = (uint64_t)(lo & ~0xfu);
  if (((lo >> 1) & 3u) == 2u) {
    uint32_t hi = pm_metal_bus_pci_read32(bus, dev, func, (uint8_t)(off + 4));
    base |= (uint64_t)hi << 32;
    if (bars_consumed != NULL) {
      *bars_consumed = 2;
    }
  }
  return base;
}

uint16_t pm_metal_bus_pci_bar_io(uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index)
{
  uint8_t  off = (uint8_t)(PCI_BAR0 + bar_index * 4);
  uint32_t bar = pm_metal_bus_pci_read32(bus, dev, func, off);

  if ((bar & 1u) == 0) {
    return 0;
  }
  return (uint16_t)(bar & ~0x3u);
}

int pm_metal_bus_pci_find(
  uint16_t vendor, uint16_t device, uint8_t *bus_out, uint8_t *dev_out, uint8_t *func_out)
{
  uint16_t bus;
  uint8_t  dev;
  uint8_t  func;
  uint8_t  hdr;
  uint8_t  fmax;

  for (bus = 0; bus < 256; bus++) {
    for (dev = 0; dev < 32; dev++) {
      uint16_t ven;

      ven = pm_metal_bus_pci_read16((uint8_t)bus, dev, 0, PCI_VENDOR_ID_OFFSET);
      if (ven == 0xffff) {
        continue;
      }

      hdr  = pm_metal_bus_pci_read8((uint8_t)bus, dev, 0, PCI_HEADER_TYPE_OFFSET);
      fmax = (hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (func = 0; func < fmax; func++) {
        ven = pm_metal_bus_pci_read16((uint8_t)bus, dev, func, PCI_VENDOR_ID_OFFSET);
        if (ven != vendor) {
          continue;
        }

        if (pm_metal_bus_pci_read16((uint8_t)bus, dev, func, PCI_DEVICE_ID_OFFSET) == device) {
          if (bus_out != NULL) {
            *bus_out = (uint8_t)bus;
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

int pm_metal_bus_pci_find_class(
  uint8_t base_class, uint8_t subclass, uint8_t *bus_out, uint8_t *dev_out, uint8_t *func_out)
{
  uint16_t bus;
  uint8_t  dev;
  uint8_t  func;
  uint8_t  hdr;
  uint8_t  fmax;

  for (bus = 0; bus < 256; bus++) {
    for (dev = 0; dev < 32; dev++) {
      uint16_t ven;

      ven = pm_metal_bus_pci_read16((uint8_t)bus, dev, 0, PCI_VENDOR_ID_OFFSET);
      if (ven == 0xffff) {
        continue;
      }

      hdr  = pm_metal_bus_pci_read8((uint8_t)bus, dev, 0, PCI_HEADER_TYPE_OFFSET);
      fmax = (hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (func = 0; func < fmax; func++) {
        uint32_t id;
        uint8_t  cls;
        uint8_t  sub;

        ven = pm_metal_bus_pci_read16((uint8_t)bus, dev, func, PCI_VENDOR_ID_OFFSET);
        if (ven == 0xffff) {
          continue;
        }

        id  = pm_metal_bus_pci_read32((uint8_t)bus, dev, func, PCI_REVISION_ID_OFFSET);
        cls = (uint8_t)((id >> 24) & 0xffu);
        sub = (uint8_t)((id >> 16) & 0xffu);
        if (cls != base_class || sub != subclass) {
          continue;
        }

        if (bus_out != NULL) {
          *bus_out = (uint8_t)bus;
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
