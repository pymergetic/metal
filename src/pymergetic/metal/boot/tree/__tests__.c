/* pymergetic.metal.boot.tree — print after boot_run. */
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/boot/tree/__types__.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.boot.tree test: %s\n", why);
    return 1;
}

int32_t pm_metal_boot_tree_tests(void) {
    if (pm_mod_boot_count() == 0) {
        return fail("empty");
    }
    if (pm_metal_boot_msg_attached(PM_METAL_BOOT_SURF_TREE) < 8u) {
        return fail("tree faces");
    }
    if (pm_metal_boot_msg_attached(PM_METAL_BOOT_SURF_MOTD) < 3u) {
        return fail("motd faces");
    }
    if (pm_metal_boot_tree_print() != 0) {
        return fail("print");
    }
    pm_metal_boot_motd();
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.boot.tree, tests, pm_metal_boot_tree_tests);
