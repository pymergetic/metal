/* pymergetic.metal.net.wg — wg0↔wg1 handshake, then UDP on the tunnel. */
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/wg.h"
#include "pymergetic/metal/net/wg/__crypto__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WG4 0x0a080001u

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.wg test: %s\n", why);
    return 1;
}

static int32_t case_no_keys(void) {
    if (pm_metal_net_wg_init(NULL) != -1) {
        return fail("init null");
    }
    if (pm_metal_net_wg_up() != -1) {
        return fail("up without handshake");
    }
    if (pm_metal_net_wg_handshake() != -1) {
        return fail("handshake without peers");
    }
    return 0;
}

static int32_t case_mismatch(void) {
    uint8_t k0[32];
    uint8_t k1[32];
    uint8_t bad[32];
    memset(k0, 0x11, sizeof(k0));
    memset(k1, 0x22, sizeof(k1));
    memset(bad, 0x33, sizeof(bad));
    if (pm_metal_net_wg_peer(0, k0, k1) != 0 || pm_metal_net_wg_peer(1, k1, bad) != 0) {
        return fail("peer mismatch setup");
    }
    if (pm_metal_net_wg_handshake() != -1) {
        return fail("handshake accepted bad remote");
    }
    return 0;
}

static int32_t case_tunnel_udp(void) {
    uint8_t k0[32];
    uint8_t k1[32];
    memset(k0, 0x41, sizeof(k0));
    memset(k1, 0x42, sizeof(k1));
    k0[0] = 1;
    k1[0] = 2;
    if (pm_metal_net_wg_peer(0, k0, k1) != 0 || pm_metal_net_wg_peer(1, k1, k0) != 0) {
        return fail("peer");
    }
    if (pm_metal_net_wg_handshake() != 0) {
        return fail("handshake");
    }
    if (pm_metal_net_wg_up() != 0 || pm_metal_net_ip_if_up(WG4) != 0) {
        return fail("up");
    }
    int32_t a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (a < 0 || b < 0) {
        return fail("socket");
    }
    if (pm_metal_net_ip_bind(a, WG4, 51820) != 0 || pm_metal_net_ip_bind(b, WG4, 51821) != 0) {
        return fail("bind");
    }
    const uint8_t msg[] = { 'w', 'g' };
    if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), WG4, 51821) != 2) {
        return fail("sendto");
    }
    pm_metal_net_ip_pump();
    uint8_t buf[8];
    uint16_t port = 0;
    int32_t n = pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port);
    (void)pm_metal_net_ip_close(a);
    (void)pm_metal_net_ip_close(b);
    if (n != 2 || buf[0] != 'w' || port != 51820) {
        return fail("recvfrom");
    }
    return 0;
}

int32_t pm_metal_net_wg_tests(void) {
    if (pm_metal_wg_crypto_selftest() != 0) {
        return fail("crypto selftest");
    }
    if (case_no_keys() != 0) {
        return 1;
    }
    if (case_mismatch() != 0) {
        return 1;
    }
    if (case_tunnel_udp() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.wg, tests, pm_metal_net_wg_tests);
