#ifndef PYMERGETIC_METAL_BUS_PCI_PCI_H_
#define PYMERGETIC_METAL_BUS_PCI_PCI_H_

#include <stdint.h>

uint32_t pm_bios_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pm_bios_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t  pm_bios_pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void     pm_bios_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t val);
void     pm_bios_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

/** Enable memory space + bus master. */
void pm_bios_pci_enable_mem_bm(uint8_t bus, uint8_t dev, uint8_t func);

/** Enable I/O space + bus master (AC97 mixer / NABM). */
void pm_bios_pci_enable_io_bm(uint8_t bus, uint8_t dev, uint8_t func);

/**
 * Read BAR and return MMIO base (physical = identity). Returns 0 on I/O BAR.
 * For 64-bit BARs consumes bar and bar+1.
 */
uint64_t pm_bios_pci_bar_mmio(
  uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index, uint8_t *bars_consumed);

/**
 * Read I/O BAR base (port). Returns 0 if BAR is MMIO / empty.
 */
uint16_t pm_bios_pci_bar_io(uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index);

/** Scan buses 0-7 for ven:did. Returns 0 when found. */
int pm_bios_pci_find(
  uint16_t vendor, uint16_t device, uint8_t *bus_out, uint8_t *dev_out, uint8_t *func_out);

/**
 * Scan for PCI class/subclass (e.g. 0x04/0x01 = multimedia audio).
 * Returns 0 when found.
 */
int pm_bios_pci_find_class(
  uint8_t base_class, uint8_t subclass, uint8_t *bus_out, uint8_t *dev_out, uint8_t *func_out);

#endif
