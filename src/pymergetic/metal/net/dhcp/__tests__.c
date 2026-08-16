/* pymergetic.metal.net.dhcp — DISCOVER/OFFER on lo, then if_up. */
#include "pymergetic/metal/net/dhcp.h"
#include "pymergetic/metal/net/ip.h"

#include <stdint.h>
#include <stdio.h>

#define LO4 0x7f000001u
#define OFFER 0x0a00000au
#define DHCP_PORT 67

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.dhcp test: %s\n", why);
    return 1;
}

int32_t pm_metal_net_dhcp_tests(void) {
    uint32_t yi = 0;
    if (pm_metal_net_dhcp_set_offer(OFFER) != 0) {
        return fail("offer");
    }
    if (pm_metal_net_dhcp_listen(LO4, DHCP_PORT) != 0) {
        return fail("listen");
    }
    if (pm_metal_net_dhcp_discover(LO4, DHCP_PORT, &yi) != 0) {
        return fail("discover");
    }
    if (yi != OFFER) {
        return fail("yiaddr");
    }
    if (pm_metal_net_ip_if_up(yi) != 0) {
        return fail("if_up");
    }
    return 0;
}
