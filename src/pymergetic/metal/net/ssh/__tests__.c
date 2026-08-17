/* pymergetic.metal.net.ssh — banner, KEXINIT names, then ECDH reply. */
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ssh.h"
#include "pymergetic/metal/net/wg/__crypto__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define SSH_PORT 2222

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.ssh test: %s\n", why);
    return 1;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t put_str(uint8_t *p, const char *s) {
    uint32_t n = (uint32_t)strlen(s);
    put_be32(p, n);
    memcpy(p + 4, s, n);
    return 4u + n;
}

static uint32_t put_bytes(uint8_t *p, const uint8_t *s, uint32_t n) {
    put_be32(p, n);
    memcpy(p + 4, s, n);
    return 4u + n;
}

static int32_t send_pkt(int32_t fd, const uint8_t *payload, uint32_t n) {
    uint8_t pkt[512];
    uint32_t pad = 8u - ((1u + n) % 8u);
    uint32_t plen;
    if (pad < 4u) {
        pad += 8u;
    }
    plen = 1u + n + pad;
    put_be32(pkt, plen);
    pkt[4] = (uint8_t)pad;
    memcpy(pkt + 5, payload, n);
    memset(pkt + 5 + n, 0, pad);
    if (pm_metal_net_ip_send(fd, pkt, 4u + plen) != (int32_t)(4u + plen)) {
        return -1;
    }
    return 0;
}

static int32_t recv_pkt(int32_t fd, uint8_t *payload, uint32_t *n) {
    uint8_t hdr[5];
    uint32_t plen;
    uint32_t pad;
    uint32_t pay;
    int32_t got = 0;
    while (got < 5) {
        int32_t k = pm_metal_net_ip_recv(fd, hdr + got, (uint32_t)(5 - got));
        if (k <= 0) {
            return -1;
        }
        got += k;
    }
    plen = get_be32(hdr);
    pad = hdr[4];
    if (plen < 5u || 1u + pad >= plen) {
        return -1;
    }
    pay = plen - 1u - pad;
    if (pay > *n) {
        return -1;
    }
    got = 0;
    while ((uint32_t)got < pay + pad) {
        int32_t k = pm_metal_net_ip_recv(fd, payload + got, pay + pad - (uint32_t)got);
        if (k <= 0) {
            return -1;
        }
        got += k;
    }
    *n = pay;
    return 0;
}

static int32_t has_name(const uint8_t *pay, uint32_t n, const char *want) {
    uint32_t i;
    uint32_t w = (uint32_t)strlen(want);
    if (n < w) {
        return 0;
    }
    for (i = 0; i + w <= n; i++) {
        if (memcmp(pay + i, want, w) == 0) {
            return 1;
        }
    }
    return 0;
}

int32_t pm_metal_net_ssh_tests(void) {
    const char *ident = pm_metal_net_ssh_ident();
    uint8_t buf[512];
    uint8_t pay[400];
    uint8_t eph[32];
    uint8_t qc[32];
    int32_t cl;
    int32_t n;
    uint32_t want;
    uint32_t pn;
    uint32_t off;
    if (ident == NULL) {
        return fail("ident");
    }
    want = (uint32_t)strlen(ident);
    if (pm_metal_net_ssh_listen(LO4, SSH_PORT) != 0) {
        return fail("listen");
    }
    cl = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (cl < 0 || pm_metal_net_ip_connect(cl, LO4, SSH_PORT) != 1) {
        return fail("connect");
    }
    (void)pm_metal_net_ssh_poll();
    n = pm_metal_net_ip_recv(cl, buf, sizeof(buf));
    if (n != (int32_t)want || memcmp(buf, ident, want) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("banner");
    }
    if (pm_metal_net_ip_send(cl, (const uint8_t *)"SSH-2.0-test\r\n", 14) != 14) {
        (void)pm_metal_net_ip_close(cl);
        return fail("client ident");
    }
    (void)pm_metal_net_ssh_poll();
    pn = sizeof(pay);
    if (recv_pkt(cl, pay, &pn) != 0 || pn < 17u || pay[0] != 20) {
        (void)pm_metal_net_ip_close(cl);
        return fail("kexinit");
    }
    if (!has_name(pay, pn, "curve25519-sha256") || !has_name(pay, pn, "ecdsa-sha2-nistp256")) {
        (void)pm_metal_net_ip_close(cl);
        return fail("kex names");
    }
    off = 0;
    pay[off++] = 20;
    memset(pay + off, 0xb0, 16);
    off += 16;
    off += put_str(pay + off, "curve25519-sha256");
    off += put_str(pay + off, "ecdsa-sha2-nistp256");
    off += put_str(pay + off, "none");
    off += put_str(pay + off, "none");
    off += put_str(pay + off, "none");
    off += put_str(pay + off, "none");
    off += put_str(pay + off, "none");
    off += put_str(pay + off, "none");
    off += put_str(pay + off, "");
    off += put_str(pay + off, "");
    pay[off++] = 0;
    put_be32(pay + off, 0);
    off += 4;
    if (send_pkt(cl, pay, off) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("client kexinit");
    }
    memset(eph, 7, sizeof(eph));
    pm_metal_wg_x25519_base(qc, eph);
    off = 0;
    pay[off++] = 30;
    off += put_bytes(pay + off, qc, 32);
    if (send_pkt(cl, pay, off) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("ecdh init");
    }
    (void)pm_metal_net_ssh_poll();
    pn = sizeof(pay);
    if (recv_pkt(cl, pay, &pn) != 0 || pn < 40u || pay[0] != 31) {
        (void)pm_metal_net_ip_close(cl);
        return fail("ecdh reply");
    }
    pn = sizeof(pay);
    if (recv_pkt(cl, pay, &pn) != 0 || pn != 1u || pay[0] != 21) {
        (void)pm_metal_net_ip_close(cl);
        return fail("newkeys");
    }
    (void)pm_metal_net_ip_close(cl);
    if (pm_metal_net_ssh_kex_ok() != 1) {
        return fail("kex_ok");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.ssh, tests, pm_metal_net_ssh_tests);
