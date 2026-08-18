/* pymergetic.metal.net.ssh — banner, kex, aes128-ctr session onto console #2. */
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ssh.h"
#include "pymergetic/metal/net/wg/__crypto__.h"
#include "pymergetic/wasmmod/guest.h"

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define SSH_PORT 2222
#define SSH_BLOCK 16u
#define SSH_MAC 32u

static mbedtls_aes_context s_aes_out;
static mbedtls_aes_context s_aes_in;
static uint8_t s_ctr_out[SSH_BLOCK];
static uint8_t s_ctr_in[SSH_BLOCK];
static uint8_t s_sb_out[SSH_BLOCK];
static uint8_t s_sb_in[SSH_BLOCK];
static size_t s_off_out;
static size_t s_off_in;
static uint8_t s_mk_out[SSH_MAC];
static uint8_t s_mk_in[SSH_MAC];
static uint32_t s_seq_out;
static uint32_t s_seq_in;
static uint32_t s_enc;

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

static uint32_t put_mpint(uint8_t *p, const uint8_t *be, uint32_t n) {
    uint32_t i = 0;
    while (i + 1u < n && be[i] == 0) {
        i++;
    }
    if ((be[i] & 0x80u) != 0) {
        put_be32(p, n - i + 1u);
        p[4] = 0;
        memcpy(p + 5, be + i, n - i);
        return 5u + (n - i);
    }
    put_be32(p, n - i);
    memcpy(p + 4, be + i, n - i);
    return 4u + (n - i);
}

static int32_t hmac_pkt(const uint8_t *key, uint32_t seq, const uint8_t *pkt, uint32_t n,
    uint8_t mac[SSH_MAC]) {
    uint8_t seqb[4];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return -1;
    }
    put_be32(seqb, seq);
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, 1) != 0 || mbedtls_md_hmac_starts(&ctx, key, SSH_MAC) != 0
        || mbedtls_md_hmac_update(&ctx, seqb, 4) != 0 || mbedtls_md_hmac_update(&ctx, pkt, n) != 0
        || mbedtls_md_hmac_finish(&ctx, mac) != 0) {
        mbedtls_md_free(&ctx);
        return -1;
    }
    mbedtls_md_free(&ctx);
    return 0;
}

static int32_t send_pkt(int32_t fd, const uint8_t *payload, uint32_t n) {
    uint8_t pkt[512];
    uint8_t mac[SSH_MAC];
    uint32_t block = s_enc ? SSH_BLOCK : 8u;
    uint32_t pad = block - ((1u + n) % block);
    uint32_t plen;
    uint32_t wire;
    if (pad < 4u) {
        pad += block;
    }
    plen = 1u + n + pad;
    put_be32(pkt, plen);
    pkt[4] = (uint8_t)pad;
    memcpy(pkt + 5, payload, n);
    memset(pkt + 5 + n, 0, pad);
    wire = 4u + plen;
    if (s_enc) {
        if (hmac_pkt(s_mk_out, s_seq_out, pkt, wire, mac) != 0) {
            return -1;
        }
        if (mbedtls_aes_crypt_ctr(&s_aes_out, wire, &s_off_out, s_ctr_out, s_sb_out, pkt, pkt)
            != 0) {
            return -1;
        }
        memcpy(pkt + wire, mac, SSH_MAC);
        wire += SSH_MAC;
    }
    if (pm_metal_net_ip_send(fd, pkt, wire) != (int32_t)wire) {
        return -1;
    }
    s_seq_out++;
    return 0;
}

static int32_t recv_all(int32_t fd, uint8_t *p, uint32_t n) {
    uint32_t got = 0;
    while (got < n) {
        int32_t k = pm_metal_net_ip_recv(fd, p + got, n - got);
        if (k <= 0) {
            return -1;
        }
        got += (uint32_t)k;
    }
    return 0;
}

static int32_t recv_pkt(int32_t fd, uint8_t *payload, uint32_t *n) {
    uint8_t pkt[1200];
    uint32_t plen;
    uint32_t pad;
    uint32_t pay;
    uint32_t wire;
    if (!s_enc) {
        if (recv_all(fd, pkt, 5) != 0) {
            return -1;
        }
        plen = get_be32(pkt);
        pad = pkt[4];
        if (plen < 5u || 1u + pad >= plen) {
            return -1;
        }
        pay = plen - 1u - pad;
        if (pay > *n || recv_all(fd, pkt + 5, pay + pad) != 0) {
            return -1;
        }
        memcpy(payload, pkt + 5, pay);
        *n = pay;
        s_seq_in++;
        return 0;
    }
    if (recv_all(fd, pkt, SSH_BLOCK) != 0) {
        return -1;
    }
    {
        uint8_t peek[SSH_BLOCK];
        uint8_t ctr[SSH_BLOCK];
        uint8_t sb[SSH_BLOCK];
        size_t off = s_off_in;
        memcpy(ctr, s_ctr_in, SSH_BLOCK);
        memcpy(sb, s_sb_in, SSH_BLOCK);
        if (mbedtls_aes_crypt_ctr(&s_aes_in, SSH_BLOCK, &off, ctr, sb, pkt, peek) != 0) {
            return -1;
        }
        plen = get_be32(peek);
    }
    if (plen < SSH_BLOCK || plen > 1024u) {
        return -1;
    }
    wire = 4u + plen;
    if (recv_all(fd, pkt + SSH_BLOCK, wire - SSH_BLOCK + SSH_MAC) != 0) {
        return -1;
    }
    if (mbedtls_aes_crypt_ctr(&s_aes_in, wire, &s_off_in, s_ctr_in, s_sb_in, pkt, pkt) != 0) {
        return -1;
    }
    {
        uint8_t mac[SSH_MAC];
        if (hmac_pkt(s_mk_in, s_seq_in, pkt, wire, mac) != 0 || memcmp(mac, pkt + wire, SSH_MAC) != 0) {
            return -1;
        }
    }
    pad = pkt[4];
    if (pad < 4u || 1u + pad >= plen) {
        return -1;
    }
    pay = plen - 1u - pad;
    if (pay > *n) {
        return -1;
    }
    memcpy(payload, pkt + 5, pay);
    *n = pay;
    s_seq_in++;
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

static void k_to_be(uint8_t be[32], const uint8_t le[32]) {
    uint32_t i;
    for (i = 0; i < 32u; i++) {
        be[i] = le[31u - i];
    }
}

static void h_bytes(mbedtls_sha256_context *h, const uint8_t *p, uint32_t n) {
    uint8_t len[4];
    put_be32(len, n);
    (void)mbedtls_sha256_update(h, len, 4);
    if (n != 0) {
        (void)mbedtls_sha256_update(h, p, n);
    }
}

static int32_t kdf_one(uint8_t *out, uint32_t n, const uint8_t *kmp, uint32_t kn, const uint8_t *sid,
    uint8_t letter) {
    uint8_t hash[32];
    mbedtls_sha256_context h;
    mbedtls_sha256_init(&h);
    if (mbedtls_sha256_starts(&h, 0) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    (void)mbedtls_sha256_update(&h, kmp, kn);
    (void)mbedtls_sha256_update(&h, sid, 32);
    (void)mbedtls_sha256_update(&h, &letter, 1);
    (void)mbedtls_sha256_update(&h, sid, 32);
    if (mbedtls_sha256_finish(&h, hash) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    mbedtls_sha256_free(&h);
    memcpy(out, hash, n);
    return 0;
}

static int32_t client_keys(const uint8_t *is, uint32_t isn, const uint8_t *ic, uint32_t icn,
    const uint8_t *ks, uint32_t ksn, const uint8_t *qc, const uint8_t *qs, const uint8_t *eph) {
    uint8_t k_le[32];
    uint8_t k_be[32];
    uint8_t kmp[40];
    uint8_t sid[32];
    uint8_t iv_out[SSH_BLOCK];
    uint8_t iv_in[SSH_BLOCK];
    uint8_t ek_out[16];
    uint8_t ek_in[16];
    uint32_t kn;
    mbedtls_sha256_context h;
    static const uint8_t vc[] = "SSH-2.0-test";
    static const uint8_t vs[] = "SSH-2.0-metal";
    if (pm_metal_wg_x25519(k_le, eph, qs) != 0) {
        return -1;
    }
    k_to_be(k_be, k_le);
    kn = put_mpint(kmp, k_be, 32);
    mbedtls_sha256_init(&h);
    if (mbedtls_sha256_starts(&h, 0) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    h_bytes(&h, vc, (uint32_t)(sizeof(vc) - 1u));
    h_bytes(&h, vs, (uint32_t)(sizeof(vs) - 1u));
    h_bytes(&h, ic, icn);
    h_bytes(&h, is, isn);
    h_bytes(&h, ks, ksn);
    h_bytes(&h, qc, 32);
    h_bytes(&h, qs, 32);
    (void)mbedtls_sha256_update(&h, kmp, kn);
    if (mbedtls_sha256_finish(&h, sid) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    mbedtls_sha256_free(&h);
    if (kdf_one(iv_out, SSH_BLOCK, kmp, kn, sid, 'A') != 0
        || kdf_one(iv_in, SSH_BLOCK, kmp, kn, sid, 'B') != 0
        || kdf_one(ek_out, 16, kmp, kn, sid, 'C') != 0 || kdf_one(ek_in, 16, kmp, kn, sid, 'D') != 0
        || kdf_one(s_mk_out, SSH_MAC, kmp, kn, sid, 'E') != 0
        || kdf_one(s_mk_in, SSH_MAC, kmp, kn, sid, 'F') != 0) {
        return -1;
    }
    mbedtls_aes_init(&s_aes_out);
    mbedtls_aes_init(&s_aes_in);
    if (mbedtls_aes_setkey_enc(&s_aes_out, ek_out, 128) != 0
        || mbedtls_aes_setkey_enc(&s_aes_in, ek_in, 128) != 0) {
        return -1;
    }
    memcpy(s_ctr_out, iv_out, SSH_BLOCK);
    memcpy(s_ctr_in, iv_in, SSH_BLOCK);
    memset(s_sb_out, 0, sizeof(s_sb_out));
    memset(s_sb_in, 0, sizeof(s_sb_in));
    s_off_out = 0;
    s_off_in = 0;
    return 0;
}

int32_t pm_metal_net_ssh_tests(void) {
    const char *ident = pm_metal_net_ssh_ident();
    uint8_t buf[512];
    uint8_t pay[400];
    uint8_t is[400];
    uint8_t ic[400];
    uint8_t eph[32];
    uint8_t qc[32];
    uint8_t qs[32];
    uint8_t ks[200];
    int32_t cl;
    int32_t n;
    uint32_t want;
    uint32_t pn;
    uint32_t off;
    uint32_t isn = 0;
    uint32_t icn = 0;
    uint32_t ksn = 0;
    s_enc = 0;
    s_seq_in = 0;
    s_seq_out = 0;
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
    if (!has_name(pay, pn, "curve25519-sha256") || !has_name(pay, pn, "aes128-ctr")
        || !has_name(pay, pn, "hmac-sha2-256")) {
        (void)pm_metal_net_ip_close(cl);
        return fail("kex names");
    }
    memcpy(is, pay, pn);
    isn = pn;
    off = 0;
    pay[off++] = 20;
    memset(pay + off, 0xb0, 16);
    off += 16;
    off += put_str(pay + off, "curve25519-sha256");
    off += put_str(pay + off, "ecdsa-sha2-nistp256");
    off += put_str(pay + off, "aes128-ctr");
    off += put_str(pay + off, "aes128-ctr");
    off += put_str(pay + off, "hmac-sha2-256");
    off += put_str(pay + off, "hmac-sha2-256");
    off += put_str(pay + off, "none");
    off += put_str(pay + off, "none");
    off += put_str(pay + off, "");
    off += put_str(pay + off, "");
    pay[off++] = 0;
    put_be32(pay + off, 0);
    off += 4;
    memcpy(ic, pay, off);
    icn = off;
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
    {
        uint32_t o = 1;
        uint32_t ln;
        ln = get_be32(pay + o);
        o += 4;
        if (o + ln + 4u > pn) {
            (void)pm_metal_net_ip_close(cl);
            return fail("ks");
        }
        memcpy(ks, pay + o, ln);
        ksn = ln;
        o += ln;
        ln = get_be32(pay + o);
        o += 4;
        if (ln != 32u || o + ln > pn) {
            (void)pm_metal_net_ip_close(cl);
            return fail("qs");
        }
        memcpy(qs, pay + o, 32);
    }
    pn = sizeof(pay);
    if (recv_pkt(cl, pay, &pn) != 0 || pn != 1u || pay[0] != 21) {
        (void)pm_metal_net_ip_close(cl);
        return fail("newkeys");
    }
    if (pm_metal_net_ssh_kex_ok() != 1) {
        (void)pm_metal_net_ip_close(cl);
        return fail("kex_ok");
    }
    if (client_keys(is, isn, ic, icn, ks, ksn, qc, qs, eph) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("client keys");
    }
    pay[0] = 21;
    if (send_pkt(cl, pay, 1) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("client newkeys");
    }
    s_enc = 1;
    off = 0;
    pay[off++] = 5;
    off += put_str(pay + off, "ssh-userauth");
    if (send_pkt(cl, pay, off) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("service");
    }
    (void)pm_metal_net_ssh_poll();
    pn = sizeof(pay);
    if (recv_pkt(cl, pay, &pn) != 0 || pn < 5u || pay[0] != 6) {
        (void)pm_metal_net_ip_close(cl);
        return fail("service accept");
    }
    off = 0;
    pay[off++] = 50;
    off += put_str(pay + off, "metal");
    off += put_str(pay + off, "ssh-connection");
    off += put_str(pay + off, "none");
    if (send_pkt(cl, pay, off) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("userauth none");
    }
    (void)pm_metal_net_ssh_poll();
    pn = sizeof(pay);
    if (recv_pkt(cl, pay, &pn) != 0 || pay[0] != 51) {
        (void)pm_metal_net_ip_close(cl);
        return fail("auth fail none");
    }
    off = 0;
    pay[off++] = 50;
    off += put_str(pay + off, "metal");
    off += put_str(pay + off, "ssh-connection");
    off += put_str(pay + off, "password");
    pay[off++] = 0;
    off += put_str(pay + off, "metal");
    if (send_pkt(cl, pay, off) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("userauth");
    }
    (void)pm_metal_net_ssh_poll();
    pn = sizeof(pay);
    if (recv_pkt(cl, pay, &pn) != 0 || pn != 1u || pay[0] != 52) {
        (void)pm_metal_net_ip_close(cl);
        return fail("auth success");
    }
    if (pm_metal_net_ssh_auth_ok() != 1) {
        (void)pm_metal_net_ip_close(cl);
        return fail("auth_ok");
    }
    off = 0;
    pay[off++] = 90;
    off += put_str(pay + off, "session");
    put_be32(pay + off, 0);
    off += 4;
    put_be32(pay + off, 0x8000);
    off += 4;
    put_be32(pay + off, 0x4000);
    off += 4;
    if (send_pkt(cl, pay, off) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("channel open");
    }
    (void)pm_metal_net_ssh_poll();
    pn = sizeof(pay);
    if (recv_pkt(cl, pay, &pn) != 0 || pn < 17u || pay[0] != 91) {
        (void)pm_metal_net_ip_close(cl);
        return fail("channel conf");
    }
    if (pm_metal_net_ssh_channel_ok() != 1) {
        (void)pm_metal_net_ip_close(cl);
        return fail("channel_ok");
    }
    if (pm_metal_net_ssh_viewport_attach(0) == 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("attach 0");
    }
    if (pm_metal_net_ssh_viewport_attach(2) != 0 || pm_metal_net_ssh_console_id() != 2) {
        (void)pm_metal_net_ip_close(cl);
        return fail("attach 2");
    }
    {
        uint32_t n0 = pm_metal_console_line_count();
        uint32_t n2 = pm_metal_console_line_count_id(2);
        off = 0;
        pay[off++] = 94;
        put_be32(pay + off, 0);
        off += 4;
        off += put_bytes(pay + off, (const uint8_t *)"ssh\n", 4);
        if (send_pkt(cl, pay, off) != 0) {
            (void)pm_metal_net_ip_close(cl);
            return fail("channel data");
        }
        (void)pm_metal_net_ssh_poll();
        if (pm_metal_console_line_count_id(2) <= n2) {
            (void)pm_metal_net_ip_close(cl);
            return fail("console 2");
        }
        if (pm_metal_console_line_count() != n0) {
            (void)pm_metal_net_ip_close(cl);
            return fail("console 0");
        }
    }
    (void)pm_metal_net_ip_close(cl);
    mbedtls_aes_free(&s_aes_out);
    mbedtls_aes_free(&s_aes_in);
    if (pm_metal_net_ssh_up() != 0 || pm_metal_net_ssh_console_id() != 1) {
        return fail("up");
    }
    return 0;
}

/* Multi-instance: a second sshd on another port coexists; status/stop per id. */
static int32_t pm_metal_net_ssh_multi_tests(void) {
    int32_t a = pm_metal_net_ssh_listen(LO4, SSH_PORT); /* id 0, dup-safe */
    if (a != 0 || pm_metal_net_ssh_status(0) != 1) {
        return fail("dup listen");
    }
    int32_t b = pm_metal_net_ssh_listen(LO4, SSH_PORT + 1);
    if (b <= a || pm_metal_net_ssh_status(b) != 1 || pm_metal_net_ssh_count() < 1) {
        return fail("listen 2nd");
    }
    if (pm_metal_net_ssh_stop(b) != 0 || pm_metal_net_ssh_status(b) != 0) {
        return fail("stop 2nd");
    }
    if (pm_metal_net_ssh_status(0) != 1) {
        return fail("stop clobbered first");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.ssh, tests, pm_metal_net_ssh_tests);
PM_MOD_TEST_C(pymergetic.metal.net.ssh, multi_instance, pm_metal_net_ssh_multi_tests);
