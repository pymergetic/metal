/* pymergetic.metal.net.tftp — RRQ on lo UDP. */
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/tftp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define TFTP_PORT 69

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.tftp test: %s\n", why);
    return 1;
}

int32_t pm_metal_net_tftp_tests(void) {
    const uint8_t body[] = { 'h', 'i' };
    uint8_t out[8];
    uint32_t n = sizeof(out);
    if (pm_metal_net_tftp_add("hi.bin", body, 2) != 0) {
        return fail("add");
    }
    if (pm_metal_net_tftp_listen(LO4, TFTP_PORT) != 0) {
        return fail("listen");
    }
    if (pm_metal_net_tftp_get(LO4, TFTP_PORT, "hi.bin", out, &n) != 0) {
        return fail("get");
    }
    if (n != 2 || out[0] != 'h' || out[1] != 'i') {
        return fail("body");
    }
    return 0;
}
