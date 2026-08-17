/* pymergetic.metal.net.ipv6 — parse ::1 and a compressed unicast. */
#include "pymergetic/metal/net/ipv6.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.ipv6 test: %s\n", why);
    return 1;
}

int32_t pm_metal_net_ipv6_tests(void) {
    uint8_t a[16];
    uint8_t b[16];
    char txt[48];
    memset(a, 0xff, sizeof(a));
    if (pm_metal_net_ipv6_parse("::1", a) != 0) {
        return fail("parse loop");
    }
    if (pm_metal_net_ipv6_is_loopback(a) != 1) {
        return fail("loopback");
    }
    if (pm_metal_net_ipv6_parse("2001:db8::1", b) != 0) {
        return fail("parse db8");
    }
    if (b[0] != 0x20 || b[1] != 0x01 || b[2] != 0x0d || b[3] != 0xb8 || b[15] != 1) {
        return fail("db8 bytes");
    }
    if (pm_metal_net_ipv6_format(a, txt, sizeof(txt)) != 0) {
        return fail("format");
    }
    if (pm_metal_net_ipv6_parse(txt, b) != 0 || memcmp(a, b, 16) != 0) {
        return fail("roundtrip");
    }
    if (pm_metal_net_ipv6_parse(":::1", a) == 0) {
        return fail("bad");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.ipv6, tests, pm_metal_net_ipv6_tests);
