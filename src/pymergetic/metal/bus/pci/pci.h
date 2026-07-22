#ifndef PYMERGETIC_METAL_BUS_PCI_PCI_H_
#define PYMERGETIC_METAL_BUS_PCI_PCI_H_

#include <Uefi.h>

UINT32 pm_bios_pci_read32(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset);
UINT16 pm_bios_pci_read16(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset);
UINT8 pm_bios_pci_read8(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset);
VOID pm_bios_pci_write16(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset, UINT16 val);
VOID pm_bios_pci_write32(UINT8 bus, UINT8 dev, UINT8 func, UINT8 offset, UINT32 val);

/** Enable memory space + bus master. */
VOID pm_bios_pci_enable_mem_bm(UINT8 bus, UINT8 dev, UINT8 func);

/**
 * Read BAR and return MMIO base (physical = identity). Returns 0 on I/O BAR.
 * For 64-bit BARs consumes bar and bar+1.
 */
UINT64 pm_bios_pci_bar_mmio(UINT8 bus, UINT8 dev, UINT8 func, UINT8 bar_index,
			    UINT8 *bars_consumed);

/** Scan buses 0-7 for ven:did. Returns 0 when found. */
int pm_bios_pci_find(UINT16 vendor, UINT16 device, UINT8 *bus_out,
		     UINT8 *dev_out, UINT8 *func_out);

#endif
