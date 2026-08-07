#ifndef PM_METAL_BUS_PCI_H_
#define PM_METAL_BUS_PCI_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t pm_metal_bus_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pm_metal_bus_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t pm_metal_bus_pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pm_metal_bus_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t val);
void pm_metal_bus_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

void pm_metal_bus_pci_enable_mem_bm(uint8_t bus, uint8_t dev, uint8_t func);

uint64_t pm_metal_bus_pci_bar_mmio(
    uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index, uint8_t *bars_consumed);

int pm_metal_bus_pci_find(
    uint16_t vendor, uint16_t device, uint8_t *bus_out, uint8_t *dev_out, uint8_t *func_out);

#ifdef __cplusplus
}
#endif

#endif
