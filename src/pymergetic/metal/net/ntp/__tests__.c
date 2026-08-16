/* pymergetic.metal.net.ntp — SNTP on lo UDP. */
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ntp.h"

#include <stdint.h>
#include <stdio.h>

#define LO4 0x7f000001u
#define NTP_PORT 123

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.ntp test: %s\n", why);
    return 1;
}

int32_t pm_metal_net_ntp_tests(void) {
    uint32_t unix_sec = 0;
    if (pm_metal_net_ntp_listen(LO4, NTP_PORT) != 0) {
        return fail("listen");
    }
    if (pm_metal_net_ntp_query(LO4, NTP_PORT, &unix_sec) != 0) {
        return fail("query");
    }
    if (unix_sec != 1u) {
        return fail("ts");
    }
    return 0;
}
