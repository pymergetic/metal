/* pymergetic.metal.bus.pci — sim table on host; CF8 when firmware.
 * Walk is the enumerator: empty fn0 skips the slot; header type bit 7 gates fn 1..7. */
#include "pymergetic/metal/bus/pci/__exports__.h"

#include <string.h>

#if (defined(__i386__) || defined(__x86_64__)) && defined(PM_METAL_FIRMWARE)
static inline void pci_outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t pci_inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
#define PM_METAL_BUS_PCI_HAVE_CF8 1
#endif

#define PM_METAL_BUS_PCI_SIM_MAX 16u

struct pm_metal_pci_sim {
    uint32_t used;
    uint32_t bus;
    uint32_t dev;
    uint32_t fn;
    uint32_t vendor;
    uint32_t device;
    uint32_t class_rev;
    uint32_t hdr;
};

static pm_util_mem_arena_t *s_arena;
static struct pm_metal_pci_sim s_sim[PM_METAL_BUS_PCI_SIM_MAX];

int32_t pm_metal_bus_pci_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_sim, 0, sizeof(s_sim));
    return 0;
}

void pm_metal_bus_pci_deinit(void) {
    memset(s_sim, 0, sizeof(s_sim));
    s_arena = NULL;
}

int32_t pm_metal_bus_pci_sim_add(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t vendor,
    uint32_t device) {
    uint32_t i;
    if (s_arena == NULL || vendor == 0 || vendor == 0xffffu) {
        return -1;
    }
    for (i = 0; i < PM_METAL_BUS_PCI_SIM_MAX; i++) {
        if (s_sim[i].used && s_sim[i].bus == bus && s_sim[i].dev == dev && s_sim[i].fn == fn) {
            s_sim[i].vendor = vendor & 0xffffu;
            s_sim[i].device = device & 0xffffu;
            return 0;
        }
    }
    for (i = 0; i < PM_METAL_BUS_PCI_SIM_MAX; i++) {
        if (!s_sim[i].used) {
            s_sim[i].used = 1;
            s_sim[i].bus = bus;
            s_sim[i].dev = dev;
            s_sim[i].fn = fn;
            s_sim[i].vendor = vendor & 0xffffu;
            s_sim[i].device = device & 0xffffu;
            s_sim[i].class_rev = 0;
            s_sim[i].hdr = 0;
            return 0;
        }
    }
    return -1;
}

static uint32_t sim_read32(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off) {
    uint32_t i;
    uint32_t o = off & 0xfcu;
    for (i = 0; i < PM_METAL_BUS_PCI_SIM_MAX; i++) {
        if (s_sim[i].used && s_sim[i].bus == bus && s_sim[i].dev == dev && s_sim[i].fn == fn) {
            if (o == 0) {
                return s_sim[i].vendor | (s_sim[i].device << 16);
            }
            if (o == 0x08u) {
                return s_sim[i].class_rev;
            }
            if (o == 0x0cu) {
                return s_sim[i].hdr;
            }
            return 0;
        }
    }
    return 0xffffffffu;
}

uint32_t pm_metal_bus_pci_cfg_read32(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off) {
    uint32_t v;
    v = sim_read32(bus, dev, fn, off);
    if (v != 0xffffffffu) {
        return v;
    }
#if defined(PM_METAL_BUS_PCI_HAVE_CF8)
    {
        uint32_t addr = 0x80000000u | ((bus & 0xffu) << 16) | ((dev & 0x1fu) << 11)
            | ((fn & 0x7u) << 8) | (off & 0xfcu);
        pci_outl(0xCF8u, addr);
        return pci_inl(0xCFCu);
    }
#else
    return 0xffffffffu;
#endif
}

void pm_metal_bus_pci_cfg_write32(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off, uint32_t val) {
#if defined(PM_METAL_BUS_PCI_HAVE_CF8)
    uint32_t addr = 0x80000000u | ((bus & 0xffu) << 16) | ((dev & 0x1fu) << 11)
        | ((fn & 0x7u) << 8) | (off & 0xfcu);
    pci_outl(0xCF8u, addr);
    pci_outl(0xCFCu, val);
#else
    (void)bus;
    (void)dev;
    (void)fn;
    (void)off;
    (void)val;
#endif
}

int32_t pm_metal_bus_pci_walk(pm_metal_bus_pci_slot_fn cb, void *ctx) {
    uint32_t bus;
    uint32_t dev;
    uint32_t fn;
    uint32_t id;
    uint32_t vendor;
    uint32_t hdr;
    int32_t st;
    if (cb == NULL) {
        return -1;
    }
    for (bus = 0; bus < 32u; bus++) {
        for (dev = 0; dev < 32u; dev++) {
            for (fn = 0; fn < 8u; fn++) {
                id = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0);
                vendor = id & 0xffffu;
                if (vendor == 0 || vendor == 0xffffu) {
                    if (fn == 0) {
                        break;
                    }
                    continue;
                }
                st = cb(bus, dev, fn, id, ctx);
                if (st != 0) {
                    return st;
                }
                if (fn == 0) {
                    hdr = (pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x0cu) >> 16) & 0xffu;
                    if (hdr != 0xffu && (hdr & 0x80u) == 0) {
                        break;
                    }
                }
            }
        }
    }
    return 0;
}

struct pci_find_ctx {
    uint32_t vendor;
    uint32_t device;
    uint32_t n;
    uint32_t seen;
    uint32_t *bus_out;
    uint32_t *dev_out;
    uint32_t *fn_out;
    uint32_t *device_out;
    uint32_t match_device;
};

static int32_t find_slot(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t id, void *ctx) {
    struct pci_find_ctx *c = ctx;
    uint32_t vendor = id & 0xffffu;
    uint32_t device = (id >> 16) & 0xffffu;
    if (vendor != c->vendor) {
        return 0;
    }
    if (c->match_device && device != c->device) {
        return 0;
    }
    if (c->seen == c->n) {
        if (c->bus_out != NULL) {
            *c->bus_out = bus;
        }
        if (c->dev_out != NULL) {
            *c->dev_out = dev;
        }
        if (c->fn_out != NULL) {
            *c->fn_out = fn;
        }
        if (c->device_out != NULL) {
            *c->device_out = device;
        }
        return 1;
    }
    c->seen++;
    return 0;
}

int32_t pm_metal_bus_pci_find_nth(uint32_t vendor, uint32_t device, uint32_t n, uint32_t *bus_out,
    uint32_t *dev_out, uint32_t *fn_out) {
    struct pci_find_ctx c;
    int32_t st;
    if (s_arena == NULL) {
        return -1;
    }
    memset(&c, 0, sizeof(c));
    c.vendor = vendor & 0xffffu;
    c.device = device & 0xffffu;
    c.n = n;
    c.bus_out = bus_out;
    c.dev_out = dev_out;
    c.fn_out = fn_out;
    c.match_device = 1;
    st = pm_metal_bus_pci_walk(find_slot, &c);
    return st == 1 ? 0 : -1;
}

int32_t pm_metal_bus_pci_find_vendor_nth(uint32_t vendor, uint32_t n, uint32_t *bus_out,
    uint32_t *dev_out, uint32_t *fn_out, uint32_t *device_out) {
    struct pci_find_ctx c;
    int32_t st;
    if (s_arena == NULL) {
        return -1;
    }
    memset(&c, 0, sizeof(c));
    c.vendor = vendor & 0xffffu;
    c.n = n;
    c.bus_out = bus_out;
    c.dev_out = dev_out;
    c.fn_out = fn_out;
    c.device_out = device_out;
    st = pm_metal_bus_pci_walk(find_slot, &c);
    return st == 1 ? 0 : -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_init, pm_metal_bus_pci_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_deinit, pm_metal_bus_pci_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_sim_add, pm_metal_bus_pci_sim_add, int32_t(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_cfg_read32, pm_metal_bus_pci_cfg_read32, uint32_t(uint32_t, uint32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_cfg_write32, pm_metal_bus_pci_cfg_write32, void(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_walk, pm_metal_bus_pci_walk, int32_t(pm_metal_bus_pci_slot_fn, void *));
PM_MOD_EXPORT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_find_nth, pm_metal_bus_pci_find_nth, int32_t(uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_find_vendor_nth, pm_metal_bus_pci_find_vendor_nth, int32_t(uint32_t, uint32_t, uint32_t *, uint32_t *, uint32_t *, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.bus.pci, pm_metal_bus_pci_init, pm_metal_bus_pci_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.bus.pci, pymergetic.metal.dt);
