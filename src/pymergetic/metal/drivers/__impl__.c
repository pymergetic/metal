/* pymergetic.metal.drivers — class unbind + match probe (one PCI walk). */
#include "pymergetic/metal/drivers/__exports__.h"

#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/fw/memmap.h"

#include <string.h>

#ifndef PM_METAL_DRV_MAX
#define PM_METAL_DRV_MAX 64u
#endif

extern const pm_metal_drv_t __start_pm_metal_drv[] __attribute__((weak));
extern const pm_metal_drv_t __stop_pm_metal_drv[] __attribute__((weak));
extern const pm_metal_class_t __start_pm_metal_class[] __attribute__((weak));
extern const pm_metal_class_t __stop_pm_metal_class[] __attribute__((weak));

static pm_util_mem_arena_t *s_arena;
static const pm_metal_drv_t *s_extra[PM_METAL_DRV_MAX];
static const pm_metal_class_t *s_classextra[PM_METAL_DRV_MAX];
static uint32_t s_nextra;
static uint32_t s_nclassextra;
static uint32_t s_probed;

static int fqn_eq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

static int id_overlap(uint32_t a, uint32_t b) {
    return a == PM_METAL_DRV_PCI_ANY || b == PM_METAL_DRV_PCI_ANY || a == b;
}

static int pci_overlap(const pm_metal_drv_t *a, const pm_metal_drv_t *b) {
    return a->id0 == b->id0 && id_overlap(a->id1, b->id1) && id_overlap(a->id2, b->id2)
        && id_overlap(a->id3, b->id3);
}

static int id_hit(uint32_t rec, uint32_t got) {
    return rec == PM_METAL_DRV_PCI_ANY || rec == got;
}

static const pm_metal_drv_t *pci_match(const pm_metal_drv_t **recs, uint32_t n, uint32_t vendor,
    uint32_t device, uint32_t pci_class, uint32_t pci_rev) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        const pm_metal_drv_t *r = recs[i];
        if (r == NULL || r->kind != PM_METAL_DRV_KIND_PCI || r->attach == NULL) {
            continue;
        }
        if (r->id0 != vendor) {
            continue;
        }
        if (!id_hit(r->id1, device) || !id_hit(r->id2, pci_class) || !id_hit(r->id3, pci_rev)) {
            continue;
        }
        return r;
    }
    return NULL;
}

struct pci_probe_ctx {
    const pm_metal_drv_t **recs;
    uint32_t n;
};

static int32_t pci_probe_slot(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t id, void *ctx) {
    struct pci_probe_ctx *c = ctx;
    uint32_t vendor = id & 0xffffu;
    uint32_t device = (id >> 16) & 0xffffu;
    uint32_t dw = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x08u);
    uint32_t pci_class = (dw >> 8) & 0xffffffu;
    uint32_t pci_rev = dw & 0xffu;
    const pm_metal_drv_t *hit = pci_match(c->recs, c->n, vendor, device, pci_class, pci_rev);
    if (hit != NULL) {
        (void)hit->attach(PM_METAL_DT_BUS_PCI, bus, (dev << 8) | fn, vendor, device);
    }
    return 0;
}

static int32_t attach_one(const pm_metal_drv_t *rec) {
    const pm_metal_drv_t *one[1];
    struct pci_probe_ctx ctx;
    if (rec == NULL || rec->attach == NULL) {
        return -1;
    }
    if (rec->kind == PM_METAL_DRV_KIND_PCI) {
        one[0] = rec;
        ctx.recs = one;
        ctx.n = 1;
        return pm_metal_bus_pci_walk(pci_probe_slot, &ctx);
    }
    if (rec->kind == PM_METAL_DRV_KIND_ISA) {
        return rec->attach(PM_METAL_DT_BUS_ISA, rec->id0, 0, 0, 0);
    }
    if (rec->kind == PM_METAL_DRV_KIND_PLATFORM) {
        return rec->attach(PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0);
    }
    return -1;
}

static uint32_t collect_drv(const pm_metal_drv_t **out, uint32_t cap);

int32_t pm_metal_drv_add(const pm_metal_drv_t *rec) {
    uint32_t i;
    if (rec == NULL || rec->mod == NULL || rec->attach == NULL) {
        return -1;
    }
    for (i = 0; i < s_nextra; i++) {
        if (fqn_eq(s_extra[i]->mod, rec->mod) && s_extra[i]->kind == rec->kind
            && s_extra[i]->id0 == rec->id0 && s_extra[i]->id1 == rec->id1
            && s_extra[i]->id2 == rec->id2 && s_extra[i]->id3 == rec->id3) {
            return 0;
        }
    }
    if (s_nextra >= PM_METAL_DRV_MAX) {
        return -1;
    }
    s_extra[s_nextra++] = rec;
    if (s_probed) {
        if (rec->kind == PM_METAL_DRV_KIND_PCI) {
            const pm_metal_drv_t *all[PM_METAL_DRV_MAX];
            uint32_t n = collect_drv(all, PM_METAL_DRV_MAX);
            uint32_t j;
            for (j = 0; j < n; j++) {
                if (all[j] == rec) {
                    continue;
                }
                if (all[j]->kind == PM_METAL_DRV_KIND_PCI && pci_overlap(all[j], rec)
                    && !fqn_eq(all[j]->mod, rec->mod)) {
                    s_nextra--;
                    return -1;
                }
            }
        }
        if (attach_one(rec) != 0) {
            s_nextra--;
            return -1;
        }
    }
    return 0;
}

int32_t pm_metal_class_add(const pm_metal_class_t *rec) {
    uint32_t i;
    if (rec == NULL || rec->unbind_dt == NULL) {
        return -1;
    }
    for (i = 0; i < s_nclassextra; i++) {
        if (s_classextra[i]->class_id == rec->class_id) {
            return 0;
        }
    }
    if (s_nclassextra >= PM_METAL_DRV_MAX) {
        return -1;
    }
    s_classextra[s_nclassextra++] = rec;
    return 0;
}

static uint32_t collect_drv(const pm_metal_drv_t **out, uint32_t cap) {
    uint32_t i;
    uint32_t w = 0;
#if !defined(__wasm__)
    const pm_metal_drv_t *recs = __start_pm_metal_drv;
    if ((uintptr_t)(const void *)recs != 0 && (uintptr_t)(const void *)__stop_pm_metal_drv != 0) {
        uint32_t n = (uint32_t)(__stop_pm_metal_drv - __start_pm_metal_drv);
        uint32_t k;
        for (k = 0; k < n && w < cap; k++) {
            if (recs[k].mod == NULL || recs[k].attach == NULL) {
                continue;
            }
            out[w++] = &recs[k];
        }
    }
#endif
    for (i = 0; i < s_nextra && w < cap; i++) {
        uint32_t j;
        int seen = 0;
        for (j = 0; j < w; j++) {
            if (out[j] == s_extra[i]) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            out[w++] = s_extra[i];
        }
    }
    return w;
}

static uint32_t collect_class(const pm_metal_class_t **out, uint32_t cap) {
    uint32_t i;
    uint32_t w = 0;
#if !defined(__wasm__)
    const pm_metal_class_t *recs = __start_pm_metal_class;
    if ((uintptr_t)(const void *)recs != 0 && (uintptr_t)(const void *)__stop_pm_metal_class != 0) {
        uint32_t n = (uint32_t)(__stop_pm_metal_class - __start_pm_metal_class);
        uint32_t k;
        for (k = 0; k < n && w < cap; k++) {
            if (recs[k].unbind_dt == NULL) {
                continue;
            }
            out[w++] = &recs[k];
        }
    }
#endif
    for (i = 0; i < s_nclassextra && w < cap; i++) {
        uint32_t j;
        int seen = 0;
        for (j = 0; j < w; j++) {
            if (out[j] == s_classextra[i]) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            out[w++] = s_classextra[i];
        }
    }
    return w;
}

int32_t pm_metal_drivers_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    return 0;
}

void pm_metal_drivers_deinit(void) {
    s_arena = NULL;
    s_probed = 0;
}

int32_t pm_metal_drivers_unbind(int32_t dt_id) {
    const pm_metal_class_t *recs[PM_METAL_DRV_MAX];
    uint32_t n;
    uint32_t i;
    int32_t class;
    if (s_arena == NULL) {
        return -1;
    }
    class = pm_metal_dt_class(dt_id);
    if (class < 0) {
        return -1;
    }
    n = collect_class(recs, PM_METAL_DRV_MAX);
    for (i = 0; i < n; i++) {
        if (recs[i] != NULL && recs[i]->class_id == class && recs[i]->unbind_dt != NULL) {
            (void)recs[i]->unbind_dt(dt_id);
        }
    }
    return pm_metal_dt_unbind(dt_id);
}

int32_t pm_metal_drivers_probe(void) {
    const pm_metal_drv_t *recs[PM_METAL_DRV_MAX];
    uint32_t n;
    uint32_t i;
    uint32_t j;
    struct pci_probe_ctx ctx;

    if (s_arena == NULL) {
        return -1;
    }
    n = collect_drv(recs, PM_METAL_DRV_MAX);
    for (i = 0; i < n; i++) {
        if (recs[i] == NULL || recs[i]->mod == NULL || recs[i]->attach == NULL) {
            return -1;
        }
        if (recs[i]->kind != PM_METAL_DRV_KIND_PCI) {
            continue;
        }
        for (j = i + 1u; j < n; j++) {
            if (recs[j]->kind != PM_METAL_DRV_KIND_PCI) {
                continue;
            }
            if (pci_overlap(recs[i], recs[j]) && !fqn_eq(recs[i]->mod, recs[j]->mod)) {
                return -1;
            }
        }
    }

    ctx.recs = recs;
    ctx.n = n;
    if (n > 0 && pm_metal_bus_pci_walk(pci_probe_slot, &ctx) != 0) {
        return -1;
    }

    for (i = 0; i < n; i++) {
        if (recs[i]->kind == PM_METAL_DRV_KIND_ISA) {
            if (recs[i]->attach(PM_METAL_DT_BUS_ISA, recs[i]->id0, 0, 0, 0) != 0) {
                return -1;
            }
        } else if (recs[i]->kind == PM_METAL_DRV_KIND_PLATFORM) {
            if (recs[i]->attach(PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0) != 0) {
                return -1;
            }
        }
    }
    if (pm_metal_fw_memmap_probe() != 0) {
        return -1;
    }
    s_probed = 1;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers, pm_metal_drivers_init, pm_metal_drivers_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers, pm_metal_drivers_deinit, pm_metal_drivers_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers, pm_metal_drivers_unbind, pm_metal_drivers_unbind, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers, pm_metal_drivers_probe, pm_metal_drivers_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers, pm_metal_drv_add, pm_metal_drv_add, int32_t(const pm_metal_drv_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers, pm_metal_class_add, pm_metal_class_add, int32_t(const pm_metal_class_t *));

PM_MOD_BOOT_C(pymergetic.metal.drivers, pm_metal_drivers_init, pm_metal_drivers_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers, pymergetic.metal.dt);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers, pymergetic.metal.fw.memmap);
PM_MOD_BOOT_CHILD_C(pymergetic.metal.drivers, pymergetic.metal.drivers.net);
PM_MOD_BOOT_CHILD_C(pymergetic.metal.drivers, pymergetic.metal.drivers.blk);
PM_MOD_BOOT_CHILD_C(pymergetic.metal.drivers, pymergetic.metal.drivers.rtc);
