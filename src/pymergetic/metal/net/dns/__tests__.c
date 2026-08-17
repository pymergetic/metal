/* pymergetic.metal.net.dns — A lookup on lo UDP. */
#include "pymergetic/metal/net/dns.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

#define LO4 0x7f000001u
#define DNS_PORT 5353

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.dns test: %s\n", why);
    return 1;
}

static int32_t case_lookup_lo(void) {
    uint32_t addr = 0;
    if (pm_metal_net_dns_add("lo.test", LO4) != 0) {
        return fail("add");
    }
    if (pm_metal_net_dns_listen(LO4, DNS_PORT) != 0) {
        return fail("listen");
    }
    if (pm_metal_net_dns_lookup("lo.test", LO4, DNS_PORT, &addr) != 0) {
        return fail("lookup");
    }
    if (addr != LO4) {
        return fail("addr");
    }
    return 0;
}

int32_t pm_metal_net_dns_tests(void) {
    return case_lookup_lo();
}

PM_MOD_TEST_C(pymergetic.metal.net.dns, tests, pm_metal_net_dns_tests);
