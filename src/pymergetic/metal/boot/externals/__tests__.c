/* pymergetic.metal.boot.externals — linked libs, real versions. */
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.boot.externals test: %s\n", why);
    return 1;
}

static int has_lib(const char *want) {
    uint32_t n = pm_metal_external_count();
    uint32_t i;
    for (i = 0; i < n; i++) {
        const char *nm = pm_metal_external_name(i);
        const char *ver = pm_metal_external_version(i);
        if (nm != NULL && strcmp(nm, want) == 0) {
            return ver != NULL && ver[0] != 0 && strcmp(ver, "ok") != 0;
        }
    }
    return 0;
}

int32_t pm_metal_boot_externals_tests(void) {
    static const pm_metal_external_t bad_ok = { "x", "ok" };
    static const pm_metal_external_t bad_empty = { "x", "" };
    if (pm_metal_external_count() == 0u) {
        return fail("empty");
    }
    if (!has_lib("tlsf") || !has_lib("wamr") || !has_lib("wasmmod") || !has_lib("mbedtls")) {
        return fail("used libs");
    }
    if (pm_metal_external_add(NULL) == 0) {
        return fail("add null");
    }
    if (pm_metal_external_add(&bad_ok) == 0) {
        return fail("add ok");
    }
    if (pm_metal_external_add(&bad_empty) == 0) {
        return fail("add empty");
    }
    if (pm_metal_external_name(pm_metal_external_count()) != NULL) {
        return fail("name oob");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.boot.externals, tests, pm_metal_boot_externals_tests);
