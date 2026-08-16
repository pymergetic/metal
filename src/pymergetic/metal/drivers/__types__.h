/* pymergetic.metal.drivers — class unbind records + match records (linker sections). */
#ifndef PYMERGETIC_METAL_DRIVERS_TYPES_H
#define PYMERGETIC_METAL_DRIVERS_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PM_METAL_DRV_KIND_PCI = 1u,
    PM_METAL_DRV_KIND_ISA = 2u,
    PM_METAL_DRV_KIND_PLATFORM = 3u,
};

#define PM_METAL_DRV_PCI_ANY 0xffffffffu

typedef int32_t (*pm_metal_drv_attach_fn)(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3);

typedef struct pm_metal_drv {
    const char *mod;
    uint32_t kind;
    uint32_t id0; /* PCI vendor or ISA port */
    uint32_t id1; /* PCI device or ANY */
    uint32_t id2; /* PCI class (24-bit) or ANY */
    uint32_t id3; /* PCI revision or ANY */
    uint32_t bar; /* BAR index 0..5 or ANY (attach reads caps) */
    pm_metal_drv_attach_fn attach;
} pm_metal_drv_t;

typedef int32_t (*pm_metal_class_unbind_fn)(int32_t dt_id);

typedef struct pm_metal_class {
    int32_t class_id;
    pm_metal_class_unbind_fn unbind_dt;
} pm_metal_class_t;

#define PM_METAL_DRV_CAT_(a, b) a##b
#define PM_METAL_DRV_CAT(a, b) PM_METAL_DRV_CAT_(a, b)

int32_t pm_metal_drv_add(const pm_metal_drv_t *rec);
int32_t pm_metal_class_add(const pm_metal_class_t *rec);

#define PM_METAL_DRV_REG_(sym) \
    static void __attribute__((constructor)) PM_METAL_DRV_CAT(pm_metal_drv_reg_, __COUNTER__)(void) { \
        (void)pm_metal_drv_add(&(sym)); \
    }
#define PM_METAL_CLASS_REG_(sym) \
    static void __attribute__((constructor)) PM_METAL_DRV_CAT(pm_metal_class_reg_, __COUNTER__)(void) { \
        (void)pm_metal_class_add(&(sym)); \
    }

#define PM_METAL_DRV_RECORD_(sym, ...) \
    static const pm_metal_drv_t __attribute__((section("pm_metal_drv"), used, aligned(8))) \
        sym = { __VA_ARGS__ }; \
    PM_METAL_DRV_REG_(sym)

#define PM_METAL_CLASS_RECORD_(sym, ...) \
    static const pm_metal_class_t __attribute__((section("pm_metal_class"), used, aligned(8))) \
        sym = { __VA_ARGS__ }; \
    PM_METAL_CLASS_REG_(sym)

#define PM_METAL_DRV_PCI_FULL_C(mod, vendor, device, pci_class, pci_rev, bar_idx, attach_fn) \
    PM_METAL_DRV_RECORD_(PM_METAL_DRV_CAT(pm_metal_drv_, __COUNTER__), #mod, PM_METAL_DRV_KIND_PCI, \
        (uint32_t)(vendor), (uint32_t)(device), (uint32_t)(pci_class), (uint32_t)(pci_rev), \
        (uint32_t)(bar_idx), (attach_fn))

#define PM_METAL_DRV_PCI_C(mod, vendor, device, attach_fn) \
    PM_METAL_DRV_PCI_FULL_C(mod, vendor, device, PM_METAL_DRV_PCI_ANY, PM_METAL_DRV_PCI_ANY, \
        PM_METAL_DRV_PCI_ANY, attach_fn)

#define PM_METAL_DRV_PCI_VENDOR_C(mod, vendor, attach_fn) \
    PM_METAL_DRV_PCI_C(mod, vendor, PM_METAL_DRV_PCI_ANY, attach_fn)

#define PM_METAL_DRV_PCI_CLASS_C(mod, vendor, device, pci_class, attach_fn) \
    PM_METAL_DRV_PCI_FULL_C(mod, vendor, device, pci_class, PM_METAL_DRV_PCI_ANY, \
        PM_METAL_DRV_PCI_ANY, attach_fn)

#define PM_METAL_DRV_ISA_C(mod, port, attach_fn) \
    PM_METAL_DRV_RECORD_(PM_METAL_DRV_CAT(pm_metal_drv_, __COUNTER__), #mod, PM_METAL_DRV_KIND_ISA, \
        (uint32_t)(port), 0u, 0u, 0u, 0u, (attach_fn))

#define PM_METAL_DRV_PLATFORM_C(mod, attach_fn) \
    PM_METAL_DRV_RECORD_(PM_METAL_DRV_CAT(pm_metal_drv_, __COUNTER__), #mod, \
        PM_METAL_DRV_KIND_PLATFORM, 0u, 0u, 0u, 0u, 0u, (attach_fn))

#define PM_METAL_CLASS_C(class_id, unbind_fn) \
    PM_METAL_CLASS_RECORD_(PM_METAL_DRV_CAT(pm_metal_class_, __COUNTER__), (class_id), (unbind_fn))

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DRIVERS_TYPES_H */
