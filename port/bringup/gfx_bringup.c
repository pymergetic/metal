#include "gfx_bringup.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/dev/gfx/scanout.h"
#include "pymergetic/metal/shell/ui.h"

#if defined(PM_METAL_BOARD_UEFI)
#include "pymergetic/metal/boot/platform/gop.h"
#elif defined(PM_METAL_BOARD_BIOS)
#include "fb_harvest.h"
#endif

int pm_metal_gfx_bringup(void)
{
    pm_metal_scanout_bind_t bind;
    char detail[72];

    memset(&bind, 0, sizeof(bind));

#if defined(PM_METAL_BOARD_UEFI)
    {
        uint32_t *fb = NULL;
        uint32_t w = 0, h = 0, ppsl = 0;
        void *gop = NULL;
        pm_metal_boot_efi_gop_stash();
        if (pm_metal_boot_efi_gop_stash_get(&fb, &w, &h, &ppsl, &gop) != 0) {
            pm_metal_boot_tree_item("gfx", PM_METAL_BOOT_TREE_FAIL, "gop");
            return -1;
        }
        bind.fb = fb;
        bind.fb_ppsl = ppsl ? ppsl : w;
        bind.mode_w = w;
        bind.mode_h = h;
        bind.gop = gop;
        /* Post-stash we still own LFB after EBS if we keep pointer; prefer owned. */
        bind.owned = (gop == NULL) ? 1 : 0;
        if (bind.owned == 0 && gop != NULL) {
            /* Pre-EBS: gop_blt; after product usually already left BS — mark owned. */
            bind.owned = 1;
            bind.gop = NULL;
        }
    }
#elif defined(PM_METAL_BOARD_BIOS)
    if (pm_metal_bios_fb_harvest(&bind) != 0) {
        pm_metal_boot_tree_item("gfx", PM_METAL_BOOT_TREE_FAIL, "harvest");
        return -1;
    }
#else
    pm_metal_boot_tree_item("gfx", PM_METAL_BOOT_TREE_FAIL, "no board");
    return -1;
#endif

    if (pm_metal_gfx_init_from_bind(&bind) != 0) {
        pm_metal_boot_tree_item("gfx", PM_METAL_BOOT_TREE_FAIL, "bind");
        return -1;
    }

    if (pm_metal_shell_ui_attach_console0() != 0) {
        snprintf(detail, sizeof(detail), "%s %ux%u (no ui)", pm_metal_gfx_scanout_name(),
                 (unsigned)bind.mode_w, (unsigned)bind.mode_h);
        pm_metal_boot_tree_item("gfx", PM_METAL_BOOT_TREE_OK, detail);
        return 0; /* panel present still ok without VT attach */
    }

    snprintf(detail, sizeof(detail), "%s %ux%u", pm_metal_gfx_scanout_name(),
             (unsigned)bind.mode_w, (unsigned)bind.mode_h);
    pm_metal_boot_tree_item("gfx", PM_METAL_BOOT_TREE_OK, detail);
    return 0;
}
