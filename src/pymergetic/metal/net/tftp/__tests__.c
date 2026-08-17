/* pymergetic.metal.net.tftp — RRQ on lo UDP. */
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/tftp.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define TFTP_PORT 69

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.tftp test: %s\n", why);
    return 1;
}

static int32_t case_one_block(void) {
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

/* Anything past 512 bytes is a run of acknowledged blocks, and 1024 makes the
 * last one empty — the case that hangs a client which stops on a short block
 * without ever counting them. */
static int32_t case_blocks(uint32_t len) {
    uint8_t body[1200];
    uint8_t out[1200];
    uint32_t n = sizeof(out);
    uint32_t i;
    for (i = 0; i < len; i++) {
        body[i] = (uint8_t)(i * 7u + 3u);
    }
    if (pm_metal_net_tftp_add(len == 1024u ? "even.bin" : "big.bin", body, (uint16_t)len) != 0) {
        return fail("add big");
    }
    if (pm_metal_net_tftp_get(LO4, TFTP_PORT, len == 1024u ? "even.bin" : "big.bin", out, &n) != 0) {
        return fail("get big");
    }
    if (n != len) {
        return fail("big length");
    }
    for (i = 0; i < len; i++) {
        if (out[i] != body[i]) {
            return fail("big bytes");
        }
    }
    return 0;
}

static int32_t case_missing(void) {
    uint8_t out[8];
    uint32_t n = sizeof(out);
    if (pm_metal_net_tftp_get(LO4, TFTP_PORT, "nope.bin", out, &n) == 0) {
        return fail("missing file returned data");
    }
    return 0;
}

int32_t pm_metal_net_tftp_tests(void) {
    if (case_one_block() != 0) {
        return 1;
    }
    if (case_blocks(1200) != 0) {
        return 1;
    }
    if (case_blocks(1024) != 0) {
        return 1;
    }
    return case_missing();
}

PM_MOD_TEST_C(pymergetic.metal.net.tftp, tests, pm_metal_net_tftp_tests);
