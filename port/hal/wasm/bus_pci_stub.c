/*
 * Browser bus.pci — same C ABI; empty config space (absent device).
 */
#include "pymergetic/metal/bus/pci.h"

uint32_t pm_metal_bus_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    return 0xffffffffu;
}

uint16_t pm_metal_bus_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    return 0xffffu;
}

uint8_t pm_metal_bus_pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    return 0xffu;
}

void pm_metal_bus_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t val)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    (void)val;
}

void pm_metal_bus_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    (void)val;
}

void pm_metal_bus_pci_enable_mem_bm(uint8_t bus, uint8_t dev, uint8_t func)
{
    (void)bus;
    (void)dev;
    (void)func;
}

uint64_t pm_metal_bus_pci_bar_mmio(uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index,
                                   uint8_t *bars_consumed)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)bar_index;
    if (bars_consumed) {
        *bars_consumed = 0;
    }
    return 0;
}

int pm_metal_bus_pci_find(uint16_t vendor, uint16_t device, uint8_t *bus_out, uint8_t *dev_out,
                          uint8_t *func_out)
{
    (void)vendor;
    (void)device;
    if (bus_out) {
        *bus_out = 0;
    }
    if (dev_out) {
        *dev_out = 0;
    }
    if (func_out) {
        *func_out = 0;
    }
    return -1;
}
