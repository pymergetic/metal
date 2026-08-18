/* pymergetic.metal.util.tree — colored box-drawing backend sanity. */
#include "pymergetic/metal/util/tree.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.util.tree test: %s\n", why);
    return 1;
}

int32_t pm_metal_util_tree_tests(void) {
    char detail[96];
    const char *p;

    /* Root lines: pad name to the column so details line up. */
    pm_metal_util_tree_item(0, 0, 0, "abc", "ok");
    pm_metal_util_tree_item(1, 0, 0, "longer-name-than-the-pad", NULL);

    /* Shallow (< boot cap) and deep depths share the same stem grammar. */
    pm_metal_util_tree_item(0, 1, 1, "c", "sim");
    pm_metal_util_tree_item(1, 1, 0, "u", "ok");
    pm_metal_util_tree_item(0, 2, 1, "x86_64", NULL);
    pm_metal_util_tree_item(1, 2, 1, "test_b", "ok");
    pm_metal_util_tree_item(1, 4, 1, "test_c", "ok");
    pm_metal_util_tree_item(1, 4, 0, "tail", NULL);

    /* paint_detail: ok green, sim cyan, FAIL red, others plain, null-safe,
     * and the trailing reset is always emitted. */
    pm_metal_util_tree_paint_detail(detail, sizeof(detail), "ok x sim y FAIL z w");
    if (strstr(detail, "\033[32mok\033[0m") == NULL) {
        return fail("ok painted green");
    }
    if (strstr(detail, "\033[36msim\033[0m") == NULL) {
        return fail("sim painted cyan");
    }
    if (strstr(detail, "\033[31mFAIL\033[0m") == NULL) {
        return fail("FAIL painted red");
    }
    p = strstr(detail, " x ");
    if (p == NULL || strstr(detail, "\033[0mx") != NULL) {
        return fail("plain token not colored");
    }
    pm_metal_util_tree_paint_detail(detail, sizeof(detail), NULL);
    if (detail[0] != 0) {
        return fail("null paints empty");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.util.tree, tests, pm_metal_util_tree_tests);
