/* pymergetic.metal.bus.pci — config space + scan (host sim and CF8). */
#ifndef PYMERGETIC_METAL_BUS_PCI_TYPES_H
#define PYMERGETIC_METAL_BUS_PCI_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return 0 to continue, nonzero to stop (that value is returned from walk). */
typedef int32_t (*pm_metal_bus_pci_slot_fn)(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t id,
    void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BUS_PCI_TYPES_H */
