/* pymergetic.metal.dt — devices catalog on the boot.tree surface. */
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/dt.h"

#include <stdio.h>

#ifndef PM_METAL_DT_WALK
#define PM_METAL_DT_WALK 128
#endif

static const char *class_name(int32_t class) {
    switch (class) {
    case PM_METAL_DT_CLASS_NET:
        return "net";
    case PM_METAL_DT_CLASS_BLK:
        return "blk";
    case PM_METAL_DT_CLASS_RTC:
        return "rtc";
    case PM_METAL_DT_CLASS_MEM:
        return "mem";
    case PM_METAL_DT_CLASS_GFX:
        return "gfx";
    case PM_METAL_DT_CLASS_AUDIO:
        return "audio";
    case PM_METAL_DT_CLASS_INPUT:
        return "input";
    default:
        return "?";
    }
}

static const char *bus_name(int32_t bus) {
    switch (bus) {
    case PM_METAL_DT_BUS_PLATFORM:
        return "plat";
    case PM_METAL_DT_BUS_PCI:
        return "pci";
    case PM_METAL_DT_BUS_ISA:
        return "isa";
    case PM_METAL_DT_BUS_VIRTIO:
        return "virtio";
    case PM_METAL_DT_BUS_MMIO:
        return "mmio";
    default:
        return "?";
    }
}

static int32_t device_n(void) {
    int32_t i;
    int32_t n = 0;
    for (i = 0; i < PM_METAL_DT_WALK; i++) {
        if (pm_metal_dt_compat(i) == NULL || pm_metal_dt_class(i) == PM_METAL_DT_CLASS_MEM) {
            continue;
        }
        n++;
    }
    return n;
}

static void msg_devices(int last) {
    int32_t n = device_n();
    int32_t i;
    int32_t seen = 0;
    char detail[32];
    char cat[32];

    (void)last;
    pm_metal_boot_msg_count(detail, sizeof(detail), "", (unsigned)n, "node");
    pm_metal_boot_msg_item(0, 0, 0, "devices", detail);
    pm_metal_boot_msg_count(cat, sizeof(cat), "ok  ", (unsigned)n, "node");
    pm_metal_boot_msg_item(n == 0, 1, 1, "catalog", cat);
    for (i = 0; i < PM_METAL_DT_WALK && seen < n; i++) {
        char name[48];
        char bus[24];
        const char *compat;
        if (pm_metal_dt_compat(i) == NULL || pm_metal_dt_class(i) == PM_METAL_DT_CLASS_MEM) {
            continue;
        }
        compat = pm_metal_dt_compat(i);
        snprintf(name, sizeof(name), "%s/%s", class_name(pm_metal_dt_class(i)),
            compat != NULL ? compat : "-");
        snprintf(bus, sizeof(bus), "bus=%s", bus_name(pm_metal_dt_bus(i)));
        seen++;
        pm_metal_boot_msg_item(seen == n, 1, 1, name, bus);
    }
}

PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_DEVICES, msg_devices);
