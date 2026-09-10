/* pymergetic.metal.coop — cpu + async faces on the boot.tree surface. */
#include "pymergetic/metal/coop.h"
#include "pymergetic/metal/boot/tree.h"

#include <stdio.h>

static void msg_cpu(int last) {
    uint32_t n = pm_metal_coop_n_runners();
    const char *kind;
    char detail[48];
    char smp[64];

    (void)last;
    if (n == 0) {
        n = 1;
    }
    kind = pm_metal_coop_runner_kind();
    pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", n, "runner");
    pm_metal_boot_msg_item(0, 0, 0, "cpu", detail);
    if (kind != NULL && kind[0] == 's' && kind[1] == 'i' && kind[2] == 'm') {
        pm_metal_boot_msg_count(smp, sizeof(smp), "sim  ", n, "runner");
    } else {
        pm_metal_boot_msg_count(smp, sizeof(smp), "ok  ", n, "runner");
    }
    pm_metal_boot_msg_item(1, 1, 1, "smp", smp);
}

static void msg_async(int last) {
    char detail[48];
    char run[64];
    uint32_t n;
    const char *kind;

    (void)last;
    if (pm_metal_coop_ready() == 0) {
        pm_metal_boot_msg_item(0, 0, 0, "coop", "FAIL");
        pm_metal_boot_msg_fail();
        return;
    }
    n = pm_metal_coop_n_runners();
    kind = pm_metal_coop_runner_kind();
    pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", n, "runner");
    pm_metal_boot_msg_item(0, 0, 0, "coop", detail);
    if (kind != NULL && kind[0] == 's' && kind[1] == 'i' && kind[2] == 'm') {
        snprintf(run, sizeof(run), "%s", kind);
    } else {
        snprintf(run, sizeof(run), "ok  %s", kind != NULL ? kind : "runner");
    }
    pm_metal_boot_msg_item(1, 1, 1, "runners", run);
}

PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_CPU, msg_cpu);
PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_COOP, msg_async);
