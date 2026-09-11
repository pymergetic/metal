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
    static const pm_metal_external_t one_more = { "prove-lib", "1.0" };
    static pm_metal_external_node_t node_ok = { &bad_ok, NULL };
    static pm_metal_external_node_t node_empty = { &bad_empty, NULL };
    static pm_metal_external_node_t node_more = { &one_more, NULL };
    static pm_metal_external_node_t node_nil = { NULL, NULL };
    uint32_t n0;
    if (pm_metal_external_count() == 0u) {
        return fail("empty");
    }
    if (!has_lib("tlsf") || !has_lib("wamr") || !has_lib("wasmmod") || !has_lib("mbedtls")) {
        return fail("used libs");
    }
    if (pm_metal_external_attach(NULL) == 0) {
        return fail("attach null");
    }
    if (pm_metal_external_attach(&node_nil) == 0) {
        return fail("attach a node with no record");
    }
    if (pm_metal_external_attach(&node_ok) == 0) {
        return fail("attach ok");
    }
    if (pm_metal_external_attach(&node_empty) == 0) {
        return fail("attach empty");
    }
    /* The list takes one more whatever the seat was built with: there is no
     * table here to run out of, and the same node twice is still one row. */
    n0 = pm_metal_external_count();
    if (pm_metal_external_attach(&node_more) != 0
        || pm_metal_external_attach(&node_more) != 0) {
        return fail("attach one more");
    }
    if (pm_metal_external_count() != n0 + 1u || !has_lib("prove-lib")) {
        return fail("the list did not take one more");
    }
    if (pm_metal_external_detach(&node_more) != 0
        || pm_metal_external_detach(&node_more) == 0) {
        return fail("detach");
    }
    if (pm_metal_external_count() != n0 || has_lib("prove-lib")) {
        return fail("the prove left its own row on the seat");
    }
    if (pm_metal_external_name(pm_metal_external_count()) != NULL) {
        return fail("name oob");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.boot.externals, tests, pm_metal_boot_externals_tests);
