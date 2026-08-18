/* pymergetic.metal.console — console face on tree + motd surfaces. */
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/console.h"

#include <stdio.h>

static void emit_console(int depth0, int last_root) {
    char cons[48];
    uint32_t nvp;
    uint32_t v;

    (void)last_root;
    nvp = pm_metal_console_ready() ? pm_metal_console_viewport_count() : 0u;
    if (pm_metal_console_ready()) {
        char vpc[24];
        pm_metal_boot_msg_count(vpc, sizeof(vpc), "", nvp, "viewport");
        snprintf(cons, sizeof(cons), "ok  #%u/%u  %s", (unsigned)pm_metal_console_id(),
            (unsigned)pm_metal_console_count(), vpc);
    } else {
        snprintf(cons, sizeof(cons), "FAIL");
        pm_metal_boot_msg_fail();
    }
    pm_metal_boot_msg_item(0, depth0, 1, "console", cons);
    for (v = 0; v < nvp; v++) {
        char vp[48];
        const char *kind = pm_metal_console_viewport_kind(v);
        snprintf(vp, sizeof(vp), "ok  %s", kind != NULL ? kind : "?");
        pm_metal_boot_msg_item(v + 1u == nvp, depth0 + 1, 1, "viewport", vp);
    }
}

static void msg_console_tree(int last) {
    emit_console(0, last);
}

static void msg_console_motd(int last) {
    emit_console(0, last);
}

PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_CONSOLE, msg_console_tree);
PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_MOTD, PM_METAL_BOOT_MSG_MOTD_CONSOLE, msg_console_motd);
