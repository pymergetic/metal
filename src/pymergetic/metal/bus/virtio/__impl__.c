/* pymergetic.metal.bus.virtio — match + nth PCI find. Queues stay in device cards. */
#include "pymergetic/metal/bus/virtio/__exports__.h"

#include "pymergetic/metal/bus/pci.h"

static pm_util_mem_arena_t *s_arena;

int32_t pm_metal_bus_virtio_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    return 0;
}

void pm_metal_bus_virtio_deinit(void) {
    s_arena = NULL;
}

int32_t pm_metal_bus_virtio_is_net(uint32_t vendor, uint32_t device) {
    vendor &= 0xffffu;
    device &= 0xffffu;
    if (vendor != PM_METAL_BUS_VIRTIO_VENDOR) {
        return 0;
    }
    return (device == PM_METAL_BUS_VIRTIO_DEV_NET || device == PM_METAL_BUS_VIRTIO_DEV_NET_LEGACY)
        ? 1
        : 0;
}

int32_t pm_metal_bus_virtio_is_blk(uint32_t vendor, uint32_t device) {
    vendor &= 0xffffu;
    device &= 0xffffu;
    if (vendor != PM_METAL_BUS_VIRTIO_VENDOR) {
        return 0;
    }
    return (device == PM_METAL_BUS_VIRTIO_DEV_BLK || device == PM_METAL_BUS_VIRTIO_DEV_BLK_LEGACY)
        ? 1
        : 0;
}

int32_t pm_metal_bus_virtio_mmio_net_ok(volatile uint32_t *base) {
    if (base == NULL) {
        return 0;
    }
    if (base[0x000 / 4] != 0x74726976u) {
        return 0;
    }
    return base[0x008 / 4] == 1u ? 1 : 0;
}

int32_t pm_metal_bus_virtio_find_net_nth(uint32_t n, uint32_t *bus, uint32_t *dev, uint32_t *fn) {
    uint32_t seen = 0;
    uint32_t k;
    uint32_t b;
    uint32_t d;
    uint32_t f;
    if (s_arena == NULL) {
        return -1;
    }
    for (k = 0; k < 16u; k++) {
        if (pm_metal_bus_pci_find_nth(PM_METAL_BUS_VIRTIO_VENDOR, PM_METAL_BUS_VIRTIO_DEV_NET, k, &b,
                &d, &f)
            != 0) {
            break;
        }
        if (seen == n) {
            if (bus != NULL) {
                *bus = b;
            }
            if (dev != NULL) {
                *dev = d;
            }
            if (fn != NULL) {
                *fn = f;
            }
            return 0;
        }
        seen++;
    }
    for (k = 0; k < 16u; k++) {
        if (pm_metal_bus_pci_find_nth(PM_METAL_BUS_VIRTIO_VENDOR, PM_METAL_BUS_VIRTIO_DEV_NET_LEGACY,
                k, &b, &d, &f)
            != 0) {
            break;
        }
        if (seen == n) {
            if (bus != NULL) {
                *bus = b;
            }
            if (dev != NULL) {
                *dev = d;
            }
            if (fn != NULL) {
                *fn = f;
            }
            return 0;
        }
        seen++;
    }
    return -1;
}

int32_t pm_metal_bus_virtio_find_blk_nth(uint32_t n, uint32_t *bus, uint32_t *dev, uint32_t *fn) {
    uint32_t seen = 0;
    uint32_t k;
    uint32_t b;
    uint32_t d;
    uint32_t f;
    if (s_arena == NULL) {
        return -1;
    }
    for (k = 0; k < 16u; k++) {
        if (pm_metal_bus_pci_find_nth(PM_METAL_BUS_VIRTIO_VENDOR, PM_METAL_BUS_VIRTIO_DEV_BLK, k, &b,
                &d, &f)
            != 0) {
            break;
        }
        if (seen == n) {
            if (bus != NULL) {
                *bus = b;
            }
            if (dev != NULL) {
                *dev = d;
            }
            if (fn != NULL) {
                *fn = f;
            }
            return 0;
        }
        seen++;
    }
    for (k = 0; k < 16u; k++) {
        if (pm_metal_bus_pci_find_nth(PM_METAL_BUS_VIRTIO_VENDOR, PM_METAL_BUS_VIRTIO_DEV_BLK_LEGACY,
                k, &b, &d, &f)
            != 0) {
            break;
        }
        if (seen == n) {
            if (bus != NULL) {
                *bus = b;
            }
            if (dev != NULL) {
                *dev = d;
            }
            if (fn != NULL) {
                *fn = f;
            }
            return 0;
        }
        seen++;
    }
    return -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.bus.virtio, pm_metal_bus_virtio_init, pm_metal_bus_virtio_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.bus.virtio, pm_metal_bus_virtio_deinit, pm_metal_bus_virtio_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.bus.virtio, pm_metal_bus_virtio_is_net, pm_metal_bus_virtio_is_net, int32_t(uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.bus.virtio, pm_metal_bus_virtio_is_blk, pm_metal_bus_virtio_is_blk, int32_t(uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.bus.virtio, pm_metal_bus_virtio_mmio_net_ok, pm_metal_bus_virtio_mmio_net_ok, int32_t(volatile uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.bus.virtio, pm_metal_bus_virtio_find_net_nth, pm_metal_bus_virtio_find_net_nth, int32_t(uint32_t, uint32_t *, uint32_t *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.bus.virtio, pm_metal_bus_virtio_find_blk_nth, pm_metal_bus_virtio_find_blk_nth, int32_t(uint32_t, uint32_t *, uint32_t *, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.bus.virtio, pm_metal_bus_virtio_init, pm_metal_bus_virtio_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.bus.virtio, pymergetic.metal.bus.pci);
