#include <stddef.h>
#include <stdint.h>

#include "io_pci.h"
#include "pymergetic/metal/bus/pci.h"

#define PCI_COMMAND_MEMORY_SPACE 0x0002u
#define PCI_COMMAND_BUS_MASTER   0x0004u
#define PCI_BAR0                 0x10u
#define PCI_COMMAND_OFFSET       0x04u
#define PCI_HEADER_TYPE_OFFSET   0x0Eu
#define HEADER_TYPE_MULTI_FUNCTION 0x80u
#define PCI_VENDOR_ID_OFFSET     0x00u
#define PCI_DEVICE_ID_OFFSET     0x02u

static uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 0x1fu) << 11) |
           ((uint32_t)(func & 0x7u) << 8) | (uint32_t)(offset & 0xfcu);
}

uint32_t pm_metal_bus_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    outl(0xcf8u, pci_addr(bus, dev, func, offset));
    return inl(0xcfcu);
}

uint16_t pm_metal_bus_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t v = pm_metal_bus_pci_read32(bus, dev, func, offset);
    return (uint16_t)((v >> ((offset & 2u) * 8u)) & 0xffffu);
}

uint8_t pm_metal_bus_pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t v = pm_metal_bus_pci_read32(bus, dev, func, offset);
    return (uint8_t)((v >> ((offset & 3u) * 8u)) & 0xffu);
}

void pm_metal_bus_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val)
{
    outl(0xcf8u, pci_addr(bus, dev, func, offset));
    outl(0xcfcu, val);
}

void pm_metal_bus_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t val)
{
    uint32_t cur = pm_metal_bus_pci_read32(bus, dev, func, offset);
    uint32_t shift = (uint32_t)(offset & 2u) * 8u;
    cur = (cur & ~(0xffffu << shift)) | ((uint32_t)val << shift);
    pm_metal_bus_pci_write32(bus, dev, func, offset, cur);
}

void pm_metal_bus_pci_enable_mem_bm(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint16_t cmd = pm_metal_bus_pci_read16(bus, dev, func, PCI_COMMAND_OFFSET);
    cmd |= (uint16_t)(PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);
    pm_metal_bus_pci_write16(bus, dev, func, PCI_COMMAND_OFFSET, cmd);
}

uint64_t pm_metal_bus_pci_bar_mmio(
    uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index, uint8_t *bars_consumed)
{
    uint8_t off = (uint8_t)(PCI_BAR0 + bar_index * 4u);
    uint32_t lo = pm_metal_bus_pci_read32(bus, dev, func, off);
    uint64_t base;

    if (bars_consumed != NULL) {
        *bars_consumed = 1;
    }
    if ((lo & 1u) != 0u) {
        return 0;
    }
    base = (uint64_t)(lo & ~0xfu);
    if (((lo >> 1) & 3u) == 2u) {
        uint32_t hi = pm_metal_bus_pci_read32(bus, dev, func, (uint8_t)(off + 4u));
        base |= (uint64_t)hi << 32;
        if (bars_consumed != NULL) {
            *bars_consumed = 2;
        }
    }
    return base;
}

int pm_metal_bus_pci_find(
    uint16_t vendor, uint16_t device, uint8_t *bus_out, uint8_t *dev_out, uint8_t *func_out)
{
    uint16_t bus;
    uint8_t dev;
    uint8_t func;

    for (bus = 0; bus < 256u; bus++) {
        for (dev = 0; dev < 32u; dev++) {
            uint16_t ven = pm_metal_bus_pci_read16((uint8_t)bus, dev, 0, PCI_VENDOR_ID_OFFSET);
            uint8_t hdr;
            uint8_t fmax;

            if (ven == 0xffffu) {
                continue;
            }

            hdr = pm_metal_bus_pci_read8((uint8_t)bus, dev, 0, PCI_HEADER_TYPE_OFFSET);
            fmax = (hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8u : 1u;
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
