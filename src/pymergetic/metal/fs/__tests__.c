/* pymergetic.metal.fs — add then read a path. */
#include "pymergetic/metal/fs.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.fs test: %s\n", why);
    return 1;
}

int32_t pm_metal_fs_tests(void) {
    const uint8_t body[] = { 'h', 'i' };
    uint8_t out[8];
    uint32_t n = sizeof(out);
    uint32_t len = 0;
    if (pm_metal_fs_add("/hi.txt", body, 2) != 0) {
        return fail("add");
    }
    if (pm_metal_fs_stat("/hi.txt", &len) != 0 || len != 2u) {
        return fail("stat");
    }
    if (pm_metal_fs_read("/hi.txt", out, &n) != 0 || n != 2u || out[0] != 'h' || out[1] != 'i') {
        return fail("read");
    }
    n = sizeof(out);
    if (pm_metal_fs_read("/nope", out, &n) == 0) {
        return fail("missing");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.fs, tests, pm_metal_fs_tests);
