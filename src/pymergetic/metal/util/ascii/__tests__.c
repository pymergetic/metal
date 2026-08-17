/* pymergetic.metal.util.ascii — FIGlet small renders METAL / MetalPython. */
#include "pymergetic/metal/util/ascii.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.util.ascii test: %s\n", why);
    return 1;
}

int32_t pm_metal_util_ascii_tests(void) {
    char out[512];
    int32_t n;
    if (pm_metal_util_ascii_bound(5) < 16u) {
        return fail("bound");
    }
    n = pm_metal_util_ascii_render("METAL", '#', out, sizeof(out));
    if (n < 20) {
        return fail("metal n");
    }
    if (strchr(out, '_') == NULL || strchr(out, '|') == NULL || strchr(out, '\n') == NULL) {
        return fail("metal glyphs");
    }
    n = pm_metal_util_ascii_render("MetalPython", '#', out, sizeof(out));
    if (n < 40 || strchr(out, '_') == NULL) {
        return fail("metalpython");
    }
    if (pm_metal_util_ascii_render(NULL, '#', out, sizeof(out)) != -1) {
        return fail("null");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.util.ascii, tests, pm_metal_util_ascii_tests);
