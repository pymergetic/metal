/* pymergetic.metal.boot.externals — externals face on the boot.tree surface. */
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/metal/boot/tree.h"

#include <stdio.h>

static void msg_externals(int last) {
    uint32_t n = pm_metal_external_count();
    uint32_t i;
    char detail[40];

    (void)last;
    if (n == 0u) {
        pm_metal_boot_msg_item(0, 0, 0, "externals", "FAIL");
        pm_metal_boot_msg_fail();
        return;
    }
    pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", n, "external");
    pm_metal_boot_msg_item(0, 0, 0, "externals", detail);
    for (i = 0; i < n; i++) {
        char ver[48];
        const char *v = pm_metal_external_version(i);
        const char *nm = pm_metal_external_name(i);
        snprintf(ver, sizeof(ver), "ok  %s", v != NULL ? v : "?");
        pm_metal_boot_msg_item(i + 1u == n, 1, 1, nm != NULL ? nm : "?", ver);
    }
}

PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_EXTERNALS, msg_externals);
