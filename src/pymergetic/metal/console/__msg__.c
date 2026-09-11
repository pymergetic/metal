/* pymergetic.metal.console — console face on tree + motd surfaces. */
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/coop.h"

#include <stdio.h>

/* Both halves of "who is looking at this console": the sinks the card writes
 * into (serial, framebuffer, an ssh session) and the readers that pull the ring
 * by cursor (the browser panel). They are different mechanisms inside the card
 * and the same thing to whoever is reading the tree — a view of this console —
 * so they are counted together and each gets a row saying which it is. A tap is
 * counted only while it keeps asking, so this line answers "who is watching
 * right now", not "who ever did". */
static void emit_console(int depth0, int last_root) {
    char cons[48];
    uint32_t nvp;
    uint32_t ntap;
    uint32_t nview;
    uint32_t now_ms;
    uint32_t v;

    now_ms = (uint32_t)(pm_metal_coop_mono_us() / 1000u);
    nvp = pm_metal_console_ready() ? pm_metal_console_viewport_count() : 0u;
    ntap = pm_metal_console_ready() ? pm_metal_console_tap_count_id(0, now_ms) : 0u;
    nview = nvp + ntap;
    if (pm_metal_console_ready()) {
        char vpc[24];
        pm_metal_boot_msg_count(vpc, sizeof(vpc), "", nview, "viewport");
        snprintf(cons, sizeof(cons), "ok  #%u/%u  %s", (unsigned)pm_metal_console_id(),
            (unsigned)pm_metal_console_count(), vpc);
    } else {
        snprintf(cons, sizeof(cons), "FAIL");
        pm_metal_boot_msg_fail();
    }
    pm_metal_boot_msg_item(last_root, depth0, 1, "console", cons);
    for (v = 0; v < nview; v++) {
        char vp[48];
        const char *kind = v < nvp
            ? pm_metal_console_viewport_kind(v)
            : pm_metal_console_tap_kind_id(0, v - nvp, now_ms);
        snprintf(vp, sizeof(vp), "ok  %s", kind != NULL ? kind : "?");
        pm_metal_boot_msg_item(v + 1u == nview, depth0 + 1, !last_root, "viewport", vp);
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
