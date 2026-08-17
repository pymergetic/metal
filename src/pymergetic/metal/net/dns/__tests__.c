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

/* resolve() is what the rest of the box calls: a literal is already an answer,
 * a name goes to the configured resolver, and with none configured it fails
 * rather than inventing an address. */
static int32_t case_resolve(void) {
    uint32_t addr = 0;
    if (pm_metal_net_dns_resolve("10.1.2.3", &addr) != 0 || addr != 0x0a010203u) {
        return fail("literal");
    }
    if (pm_metal_net_dns_resolve("10.1.2", &addr) == 0) {
        return fail("short literal accepted");
    }
    if (pm_metal_net_dns_resolve("10.1.2.300", &addr) == 0) {
        return fail("out of range literal accepted");
    }
    if (pm_metal_net_dns_server_set(0) != 0 || pm_metal_net_dns_server() != 0) {
        return fail("server clear");
    }
    if (pm_metal_net_dns_resolve("nowhere.test", &addr) == 0) {
        return fail("resolved with no resolver");
    }
    if (pm_metal_net_dns_add("cfg.test", 0x0a020304u) != 0) {
        return fail("add cfg");
    }
    /* Our own zone answers before the wire does. */
    addr = 0;
    if (pm_metal_net_dns_resolve("cfg.test", &addr) != 0 || addr != 0x0a020304u) {
        return fail("zone");
    }
    if (pm_metal_net_dns_server_set(LO4) != 0 || pm_metal_net_dns_server() != LO4) {
        return fail("server set");
    }
    return 0;
}

int32_t pm_metal_net_dns_tests(void) {
    if (case_lookup_lo() != 0) {
        return 1;
    }
    return case_resolve();
}

PM_MOD_TEST_C(pymergetic.metal.net.dns, tests, pm_metal_net_dns_tests);
