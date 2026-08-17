/* pymergetic.metal.trust — SHA-256("abc") and constant-time eq. */
#include "pymergetic/metal/trust.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.trust test: %s\n", why);
    return 1;
}

int32_t pm_metal_trust_tests(void) {
    static const uint8_t abc[] = { 'a', 'b', 'c' };
    static const uint8_t want[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22,
        0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00,
        0x15, 0xad,
    };
    uint8_t got[32];
    if (pm_metal_trust_sha256(abc, 3, got) != 0) {
        return fail("sha256");
    }
    if (pm_metal_trust_eq(got, want, 32) != 1) {
        return fail("digest");
    }
    if (pm_metal_trust_eq(got, want, 31) != 1) {
        return fail("prefix");
    }
    got[0] ^= 1u;
    if (pm_metal_trust_eq(got, want, 32) != 0) {
        return fail("neq");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.trust, tests, pm_metal_trust_tests);
