/* pymergetic.metal.net.ssh — SSH-2.0 ident + curve25519-sha256 kex on ip TCP. */
#include "pymergetic/metal/net/ssh/__exports__.h"

#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/wg/__crypto__.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/sha256.h"

#include <string.h>

#define SSH_KEXINIT 20
#define SSH_NEWKEYS 21
#define SSH_ECDH_INIT 30
#define SSH_ECDH_REPLY 31
#define RX_MAX 2048u
#define PAY_MAX 1024u

static const uint8_t s_ident[] = "SSH-2.0-metal\r\n";
static const char s_vc_default[] = "SSH-2.0-test";
static const char s_kex_name[] = "curve25519-sha256";
static const char s_hk_name[] = "ecdsa-sha2-nistp256";
static const char s_curve[] = "nistp256";
static const char s_none[] = "none";

static pm_util_mem_arena_t *s_arena;
static int32_t s_ls = -1;
static int32_t s_peer = -1;
static uint32_t s_kex_ok;
static uint32_t s_have_key;

static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static mbedtls_ecdsa_context s_hk;
static uint8_t s_ks[160];
static uint32_t s_ks_len;

static uint8_t s_is[256];
static uint32_t s_is_len;
static uint8_t s_ic[256];
static uint32_t s_ic_len;
static uint8_t s_vc[64];
static uint32_t s_vc_len;
static uint8_t s_rx[RX_MAX];
static uint32_t s_rx_len;
static uint32_t s_got_ident;
static uint32_t s_sent_kex;
static uint32_t s_got_ic;

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
    uint32_t n = 0;
    while (s[n] != 0) {
        n++;
    }
    put_be32(p, n);
    memcpy(p + 4, s, n);
    return 4u + n;
}

static uint32_t put_bytes(uint8_t *p, const uint8_t *s, uint32_t n) {
    put_be32(p, n);
    if (n != 0) {
        memcpy(p + 4, s, n);
    }
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

static int32_t rng(void *ctx, unsigned char *out, size_t n) {
    return mbedtls_ctr_drbg_random(ctx, out, n);
}

static int32_t send_pkt(const uint8_t *payload, uint32_t n) {
    uint8_t pkt[1200];
    uint32_t pad = 8u - ((1u + n) % 8u);
    uint32_t plen;
    if (pad < 4u) {
        pad += 8u;
    }
    plen = 1u + n + pad;
    if (4u + plen > sizeof(pkt)) {
        return -1;
    }
    put_be32(pkt, plen);
    pkt[4] = (uint8_t)pad;
    memcpy(pkt + 5, payload, n);
    memset(pkt + 5 + n, 0, pad);
    if (pm_metal_net_ip_send(s_peer, pkt, 4u + plen) != (int32_t)(4u + plen)) {
        return -1;
    }
    return 0;
}

static int32_t hostkey_init(void) {
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point Q;
    uint8_t q[65];
    size_t qn = 0;
    uint32_t n = 0;
    mbedtls_ecdsa_init(&s_hk);
    if (mbedtls_ecdsa_genkey(&s_hk, MBEDTLS_ECP_DP_SECP256R1, rng, &s_drbg) != 0) {
        mbedtls_ecdsa_free(&s_hk);
        return -1;
    }
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);
    if (mbedtls_ecp_export(&s_hk, &grp, &d, &Q) != 0
        || mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &qn, q, sizeof(q))
            != 0
        || qn == 0 || qn > sizeof(q)) {
        mbedtls_ecp_group_free(&grp);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_point_free(&Q);
        mbedtls_ecdsa_free(&s_hk);
        return -1;
    }
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    n += put_str(s_ks + n, s_hk_name);
    n += put_str(s_ks + n, s_curve);
    n += put_bytes(s_ks + n, q, (uint32_t)qn);
    s_ks_len = n;
    s_have_key = 1;
    return 0;
}

static int32_t send_kexinit(void) {
    uint8_t payload[400];
    uint32_t n = 0;
    payload[n++] = SSH_KEXINIT;
    if (mbedtls_ctr_drbg_random(&s_drbg, payload + n, 16) != 0) {
        return -1;
    }
    n += 16;
    n += put_str(payload + n, s_have_key ? s_kex_name : s_none);
    n += put_str(payload + n, s_have_key ? s_hk_name : s_none);
    n += put_str(payload + n, s_none);
    n += put_str(payload + n, s_none);
    n += put_str(payload + n, s_none);
    n += put_str(payload + n, s_none);
    n += put_str(payload + n, s_none);
    n += put_str(payload + n, s_none);
    n += put_str(payload + n, "");
    n += put_str(payload + n, "");
    payload[n++] = 0;
    put_be32(payload + n, 0);
    n += 4;
    if (n > sizeof(s_is)) {
        return -1;
    }
    memcpy(s_is, payload, n);
    s_is_len = n;
    if (send_pkt(payload, n) != 0) {
        return -1;
    }
    s_sent_kex = 1;
    return 0;
}

static void h_bytes(mbedtls_sha256_context *h, const uint8_t *p, uint32_t n) {
    uint8_t len[4];
    put_be32(len, n);
    (void)mbedtls_sha256_update(h, len, 4);
    if (n != 0) {
        (void)mbedtls_sha256_update(h, p, n);
    }
}

static void k_to_be(uint8_t be[32], const uint8_t le[32]) {
    uint32_t i;
    for (i = 0; i < 32u; i++) {
        be[i] = le[31u - i];
    }
}

static int32_t sign_h(const uint8_t h[32], uint8_t *sig, uint32_t *sig_len) {
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_point Q;
    uint8_t rb[48];
    uint8_t sb[48];
    size_t rn;
    size_t sn;
    uint8_t inner[128];
    uint32_t in = 0;
    uint32_t n = 0;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_ecp_point_init(&Q);
    if (mbedtls_ecp_export(&s_hk, &grp, &d, &Q) != 0
        || mbedtls_ecdsa_sign(&grp, &r, &s, &d, h, 32, rng, &s_drbg) != 0) {
        mbedtls_ecp_group_free(&grp);
        mbedtls_mpi_free(&d);
        mbedtls_mpi_free(&r);
        mbedtls_mpi_free(&s);
        mbedtls_ecp_point_free(&Q);
        return -1;
    }
    rn = mbedtls_mpi_size(&r);
    sn = mbedtls_mpi_size(&s);
    if (rn == 0 || sn == 0 || rn > sizeof(rb) || sn > sizeof(sb)
        || mbedtls_mpi_write_binary(&r, rb, rn) != 0
        || mbedtls_mpi_write_binary(&s, sb, sn) != 0) {
        mbedtls_ecp_group_free(&grp);
        mbedtls_mpi_free(&d);
        mbedtls_mpi_free(&r);
        mbedtls_mpi_free(&s);
        mbedtls_ecp_point_free(&Q);
        return -1;
    }
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecp_point_free(&Q);
    in += put_mpint(inner + in, rb, (uint32_t)rn);
    in += put_mpint(inner + in, sb, (uint32_t)sn);
    n += put_str(sig + n, s_hk_name);
    n += put_bytes(sig + n, inner, in);
    *sig_len = n;
    return 0;
}

static int32_t do_ecdh(const uint8_t *qc) {
    uint8_t eph[32];
    uint8_t qs[32];
    uint8_t k_le[32];
    uint8_t k_be[32];
    uint8_t hash[32];
    uint8_t sig[200];
    uint32_t sig_len = 0;
    uint8_t reply[512];
    uint32_t n = 0;
    uint8_t newkeys = SSH_NEWKEYS;
    mbedtls_sha256_context h;
    static const uint8_t vs[] = "SSH-2.0-metal";
    if (!s_have_key || s_is_len == 0 || s_ic_len == 0) {
        return -1;
    }
    if (mbedtls_ctr_drbg_random(&s_drbg, eph, sizeof(eph)) != 0) {
        return -1;
    }
    pm_metal_wg_x25519_base(qs, eph);
    if (pm_metal_wg_x25519(k_le, eph, qc) != 0) {
        return -1;
    }
    k_to_be(k_be, k_le);
    mbedtls_sha256_init(&h);
    if (mbedtls_sha256_starts(&h, 0) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    h_bytes(&h, s_vc, s_vc_len);
    h_bytes(&h, vs, (uint32_t)(sizeof(vs) - 1u));
    h_bytes(&h, s_ic, s_ic_len);
    h_bytes(&h, s_is, s_is_len);
    h_bytes(&h, s_ks, s_ks_len);
    h_bytes(&h, qc, 32);
    h_bytes(&h, qs, 32);
    {
        uint8_t mp[40];
        uint32_t mn = put_mpint(mp, k_be, 32);
        (void)mbedtls_sha256_update(&h, mp, mn);
    }
    if (mbedtls_sha256_finish(&h, hash) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    mbedtls_sha256_free(&h);
    if (sign_h(hash, sig, &sig_len) != 0) {
        return -1;
    }
    reply[n++] = SSH_ECDH_REPLY;
    n += put_bytes(reply + n, s_ks, s_ks_len);
    n += put_bytes(reply + n, qs, 32);
    n += put_bytes(reply + n, sig, sig_len);
    if (send_pkt(reply, n) != 0 || send_pkt(&newkeys, 1) != 0) {
        return -1;
    }
    s_kex_ok = 1;
    return 0;
}

static int32_t take_str(const uint8_t *p, uint32_t n, uint32_t *off, const uint8_t **out,
    uint32_t *out_n) {
    uint32_t ln;
    if (*off + 4u > n) {
        return -1;
    }
    ln = get_be32(p + *off);
    *off += 4u;
    if (*off + ln > n) {
        return -1;
    }
    *out = p + *off;
    *out_n = ln;
    *off += ln;
    return 0;
}

static int32_t on_payload(const uint8_t *p, uint32_t n) {
    if (n == 0) {
        return 0;
    }
    if (p[0] == SSH_KEXINIT && !s_got_ic) {
        if (n > sizeof(s_ic)) {
            return -1;
        }
        memcpy(s_ic, p, n);
        s_ic_len = n;
        s_got_ic = 1;
        return 0;
    }
    if (p[0] == SSH_ECDH_INIT && s_got_ic && !s_kex_ok) {
        uint32_t off = 1;
        const uint8_t *qc = NULL;
        uint32_t qn = 0;
        if (take_str(p, n, &off, &qc, &qn) != 0 || qn != 32u) {
            return -1;
        }
        return do_ecdh(qc);
    }
    return 0;
}

static int32_t pump_rx(void) {
    for (;;) {
        uint32_t plen;
        uint32_t pad;
        uint32_t pay;
        if (s_rx_len < 4u) {
            return 0;
        }
        plen = get_be32(s_rx);
        if (plen < 5u || plen > PAY_MAX) {
            return -1;
        }
        if (s_rx_len < 4u + plen) {
            return 0;
        }
        pad = s_rx[4];
        if (pad < 4u || 1u + pad >= plen) {
            return -1;
        }
        pay = plen - 1u - pad;
        if (on_payload(s_rx + 5, pay) != 0) {
            return -1;
        }
        memmove(s_rx, s_rx + 4u + plen, s_rx_len - (4u + plen));
        s_rx_len -= 4u + plen;
    }
}

static int32_t eat_ident(void) {
    uint32_t i;
    for (i = 0; i + 1u < s_rx_len; i++) {
        if (s_rx[i] == '\r' && s_rx[i + 1u] == '\n') {
            uint32_t n = i;
            if (n >= sizeof(s_vc)) {
                n = sizeof(s_vc) - 1u;
            }
            memcpy(s_vc, s_rx, n);
            s_vc_len = n;
            memmove(s_rx, s_rx + i + 2u, s_rx_len - (i + 2u));
            s_rx_len -= i + 2u;
            s_got_ident = 1;
            return 0;
        }
    }
    return 0;
}

int32_t pm_metal_net_ssh_init(pm_util_mem_arena_t *arena) {
    static const char pers[] = "metal-ssh";
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_ls = -1;
    s_peer = -1;
    s_kex_ok = 0;
    s_have_key = 0;
    s_is_len = 0;
    s_ic_len = 0;
    s_vc_len = 0;
    s_rx_len = 0;
    s_got_ident = 0;
    s_sent_kex = 0;
    s_got_ic = 0;
    memcpy(s_vc, s_vc_default, sizeof(s_vc_default) - 1u);
    s_vc_len = (uint32_t)(sizeof(s_vc_default) - 1u);
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_drbg);
    if (mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy, (const uint8_t *)pers,
            sizeof(pers) - 1u)
        != 0) {
        return 0;
    }
    (void)hostkey_init();
    return 0;
}

void pm_metal_net_ssh_deinit(void) {
    if (s_peer >= 0) {
        (void)pm_metal_net_ip_close(s_peer);
        s_peer = -1;
    }
    if (s_ls >= 0) {
        (void)pm_metal_net_ip_close(s_ls);
        s_ls = -1;
    }
    if (s_have_key) {
        mbedtls_ecdsa_free(&s_hk);
        s_have_key = 0;
    }
    mbedtls_ctr_drbg_free(&s_drbg);
    mbedtls_entropy_free(&s_entropy);
    s_kex_ok = 0;
    s_arena = NULL;
}

int32_t pm_metal_net_ssh_listen(uint32_t addr_be, uint16_t port) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_ls >= 0) {
        return 0;
    }
    s_ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (s_ls < 0 || pm_metal_net_ip_bind(s_ls, addr_be, port) != 0
        || pm_metal_net_ip_listen(s_ls, 1) != 0) {
        if (s_ls >= 0) {
            (void)pm_metal_net_ip_close(s_ls);
            s_ls = -1;
        }
        return -1;
    }
    return 0;
}

int32_t pm_metal_net_ssh_poll(void) {
    int32_t c;
    int32_t n;
    if (s_ls < 0) {
        return 0;
    }
    c = pm_metal_net_ip_accept(s_ls);
    if (c >= 0) {
        if (s_peer >= 0) {
            (void)pm_metal_net_ip_close(s_peer);
        }
        s_peer = c;
        s_kex_ok = 0;
        s_got_ident = 0;
        s_sent_kex = 0;
        s_got_ic = 0;
        s_rx_len = 0;
        s_ic_len = 0;
        (void)pm_metal_net_ip_send(s_peer, s_ident, (uint32_t)(sizeof(s_ident) - 1u));
    }
    if (s_peer < 0) {
        return 0;
    }
    if (s_rx_len < RX_MAX) {
        n = pm_metal_net_ip_recv(s_peer, s_rx + s_rx_len, RX_MAX - s_rx_len);
        if (n > 0) {
            s_rx_len += (uint32_t)n;
        }
    }
    if (!s_got_ident) {
        (void)eat_ident();
        if (s_got_ident && !s_sent_kex) {
            (void)send_kexinit();
        }
    }
    if (s_got_ident) {
        (void)pump_rx();
    }
    return 0;
}

const char *pm_metal_net_ssh_ident(void) {
    return (const char *)s_ident;
}

int32_t pm_metal_net_ssh_kex_ok(void) {
    return s_kex_ok ? 1 : 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_init, pm_metal_net_ssh_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_deinit, pm_metal_net_ssh_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_listen, pm_metal_net_ssh_listen, int32_t(uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_poll, pm_metal_net_ssh_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_ident, pm_metal_net_ssh_ident, const char *(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_kex_ok, pm_metal_net_ssh_kex_ok, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_init, pm_metal_net_ssh_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ssh, pymergetic.metal.net.ip);
