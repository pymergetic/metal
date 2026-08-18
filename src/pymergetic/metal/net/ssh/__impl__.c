/* pymergetic.metal.net.ssh — ident + kex, then aes128-ctr / hmac-sha2-256 / password. */
#include "pymergetic/metal/net/ssh/__exports__.h"

#include "pymergetic/metal/console.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/wg/__crypto__.h"

#include "mbedtls/aes.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#include <string.h>

#define SSH_DISCONNECT 1
#define SSH_IGNORE 2
#define SSH_SERVICE_REQUEST 5
#define SSH_SERVICE_ACCEPT 6
#define SSH_KEXINIT 20
#define SSH_NEWKEYS 21
#define SSH_ECDH_INIT 30
#define SSH_ECDH_REPLY 31
#define SSH_USERAUTH_REQUEST 50
#define SSH_USERAUTH_FAILURE 51
#define SSH_USERAUTH_SUCCESS 52
#define SSH_CHANNEL_OPEN 90
#define SSH_CHANNEL_OPEN_CONF 91
#define SSH_CHANNEL_DATA 94
#define SSH_CHANNEL_REQUEST 98
#define SSH_CHANNEL_SUCCESS 99
#define RX_MAX 2048u
#define PAY_MAX 1024u
#define SSH_CONSOLE_N 6
#define SSH_BLOCK 16u
#define SSH_MAC 32u
#define SSH_AES_KEY 16u

static const uint8_t s_ident[] = "SSH-2.0-metal\r\n";
static const char s_vc_default[] = "SSH-2.0-test";
static const char s_kex_name[] = "curve25519-sha256";
static const char s_hk_name[] = "ecdsa-sha2-nistp256";
static const char s_curve[] = "nistp256";
static const char s_aes[] = "aes128-ctr";
static const char s_hmac[] = "hmac-sha2-256";
static const char s_none[] = "none";
static const char s_password[] = "password";
static const char s_user[] = "metal";
static const char s_pass[] = "metal";

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
static uint32_t s_auth_ok;
static uint32_t s_chan_ok;
static uint32_t s_remote_chan;
static int32_t s_cons = -1;

static uint32_t s_seq_in;
static uint32_t s_seq_out;
static uint32_t s_enc_in;
static uint32_t s_enc_out;
static uint32_t s_keys_ok;
static mbedtls_aes_context s_aes_in;
static mbedtls_aes_context s_aes_out;
static uint8_t s_ctr_in[SSH_BLOCK];
static uint8_t s_ctr_out[SSH_BLOCK];
static uint8_t s_sb_in[SSH_BLOCK];
static uint8_t s_sb_out[SSH_BLOCK];
static size_t s_off_in;
static size_t s_off_out;
static uint8_t s_mk_in[SSH_MAC];
static uint8_t s_mk_out[SSH_MAC];
static uint8_t s_k_mp[40];
static uint32_t s_k_mp_len;
static uint8_t s_sid[32];

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

static int32_t str_match(const uint8_t *p, uint32_t n, const char *s) {
    uint32_t i = 0;
    while (s[i] != 0) {
        i++;
    }
    return i == n && memcmp(p, s, n) == 0 ? 1 : 0;
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

static int32_t send_pkt(const uint8_t *payload, uint32_t n) {
    uint8_t pkt[1200];
    uint8_t mac[SSH_MAC];
    uint32_t block = s_enc_out ? SSH_BLOCK : 8u;
    uint32_t pad = block - ((1u + n) % block);
    uint32_t plen;
    uint32_t wire;
    if (pad < 4u) {
        pad += block;
    }
    plen = 1u + n + pad;
    if (4u + plen + (s_enc_out ? SSH_MAC : 0u) > sizeof(pkt)) {
        return -1;
    }
    put_be32(pkt, plen);
    pkt[4] = (uint8_t)pad;
    memcpy(pkt + 5, payload, n);
    memset(pkt + 5 + n, 0, pad);
    wire = 4u + plen;
    if (s_enc_out) {
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
    if (pm_metal_net_ip_send(s_peer, pkt, wire) != (int32_t)wire) {
        return -1;
    }
    s_seq_out++;
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
    n += put_str(payload + n, s_aes);
    n += put_str(payload + n, s_aes);
    n += put_str(payload + n, s_hmac);
    n += put_str(payload + n, s_hmac);
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

static int32_t kdf_one(uint8_t *out, uint32_t n, uint8_t letter) {
    uint8_t hash[32];
    mbedtls_sha256_context h;
    mbedtls_sha256_init(&h);
    if (mbedtls_sha256_starts(&h, 0) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    (void)mbedtls_sha256_update(&h, s_k_mp, s_k_mp_len);
    (void)mbedtls_sha256_update(&h, s_sid, 32);
    (void)mbedtls_sha256_update(&h, &letter, 1);
    (void)mbedtls_sha256_update(&h, s_sid, 32);
    if (mbedtls_sha256_finish(&h, hash) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    mbedtls_sha256_free(&h);
    if (n > 32u) {
        return -1;
    }
    memcpy(out, hash, n);
    return 0;
}

static int32_t derive_keys(void) {
    uint8_t iv_in[SSH_BLOCK];
    uint8_t iv_out[SSH_BLOCK];
    uint8_t ek_in[SSH_AES_KEY];
    uint8_t ek_out[SSH_AES_KEY];
    /* A client→server IV, B server→client IV, C client enc, D server enc, E client mac, F server mac */
    if (kdf_one(iv_in, SSH_BLOCK, 'A') != 0 || kdf_one(iv_out, SSH_BLOCK, 'B') != 0
        || kdf_one(ek_in, SSH_AES_KEY, 'C') != 0 || kdf_one(ek_out, SSH_AES_KEY, 'D') != 0
        || kdf_one(s_mk_in, SSH_MAC, 'E') != 0 || kdf_one(s_mk_out, SSH_MAC, 'F') != 0) {
        return -1;
    }
    mbedtls_aes_free(&s_aes_in);
    mbedtls_aes_free(&s_aes_out);
    mbedtls_aes_init(&s_aes_in);
    mbedtls_aes_init(&s_aes_out);
    if (mbedtls_aes_setkey_enc(&s_aes_in, ek_in, 128) != 0
        || mbedtls_aes_setkey_enc(&s_aes_out, ek_out, 128) != 0) {
        return -1;
    }
    memcpy(s_ctr_in, iv_in, SSH_BLOCK);
    memcpy(s_ctr_out, iv_out, SSH_BLOCK);
    memset(s_sb_in, 0, sizeof(s_sb_in));
    memset(s_sb_out, 0, sizeof(s_sb_out));
    s_off_in = 0;
    s_off_out = 0;
    s_keys_ok = 1;
    return 0;
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
    s_k_mp_len = put_mpint(s_k_mp, k_be, 32);
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
    (void)mbedtls_sha256_update(&h, s_k_mp, s_k_mp_len);
    if (mbedtls_sha256_finish(&h, hash) != 0) {
        mbedtls_sha256_free(&h);
        return -1;
    }
    mbedtls_sha256_free(&h);
    memcpy(s_sid, hash, 32);
    if (sign_h(hash, sig, &sig_len) != 0 || derive_keys() != 0) {
        return -1;
    }
    reply[n++] = SSH_ECDH_REPLY;
    n += put_bytes(reply + n, s_ks, s_ks_len);
    n += put_bytes(reply + n, qs, 32);
    n += put_bytes(reply + n, sig, sig_len);
    if (send_pkt(reply, n) != 0 || send_pkt(&newkeys, 1) != 0) {
        return -1;
    }
    if (!s_keys_ok) {
        return -1;
    }
    s_enc_out = 1;
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

static void ssh_sink(const char *s, uint32_t n) {
    uint8_t pay[512];
    uint32_t off;
    uint32_t chunk;
    if (!s_chan_ok || s_peer < 0 || s == NULL || n == 0) {
        return;
    }
    while (n > 0u) {
        chunk = n;
        if (chunk > 400u) {
            chunk = 400u;
        }
        off = 0;
        pay[off++] = SSH_CHANNEL_DATA;
        put_be32(pay + off, s_remote_chan);
        off += 4;
        off += put_bytes(pay + off, (const uint8_t *)s, chunk);
        if (send_pkt(pay, off) != 0) {
            return;
        }
        s += chunk;
        n -= chunk;
    }
}

static int32_t send_auth_fail(void) {
    uint8_t fail[32];
    uint32_t n = 0;
    fail[n++] = SSH_USERAUTH_FAILURE;
    n += put_str(fail + n, s_password);
    fail[n++] = 0;
    return send_pkt(fail, n);
}

static int32_t on_session(const uint8_t *p, uint32_t n) {
    uint32_t off;
    const uint8_t *sv = NULL;
    uint32_t svn = 0;
    if (p[0] == SSH_NEWKEYS) {
        s_enc_in = 1;
        return 0;
    }
    if (p[0] == SSH_IGNORE || p[0] == SSH_DISCONNECT) {
        return 0;
    }
    if (p[0] == SSH_SERVICE_REQUEST) {
        uint8_t acc[64];
        uint32_t an = 0;
        off = 1;
        if (take_str(p, n, &off, &sv, &svn) != 0 || svn == 0 || svn > 32u) {
            return 0;
        }
        acc[an++] = SSH_SERVICE_ACCEPT;
        an += put_bytes(acc + an, sv, svn);
        return send_pkt(acc, an);
    }
    if (p[0] == SSH_USERAUTH_REQUEST) {
        const uint8_t *user = NULL;
        const uint8_t *svc = NULL;
        const uint8_t *meth = NULL;
        const uint8_t *pw = NULL;
        uint32_t un = 0;
        uint32_t sn = 0;
        uint32_t mn = 0;
        uint32_t pn = 0;
        uint8_t ok = SSH_USERAUTH_SUCCESS;
        off = 1;
        if (take_str(p, n, &off, &user, &un) != 0 || take_str(p, n, &off, &svc, &sn) != 0
            || take_str(p, n, &off, &meth, &mn) != 0) {
            return send_auth_fail();
        }
        if (!str_match(meth, mn, s_password) || off >= n) {
            return send_auth_fail();
        }
        off++;
        if (take_str(p, n, &off, &pw, &pn) != 0 || !str_match(user, un, s_user)
            || !str_match(pw, pn, s_pass)) {
            return send_auth_fail();
        }
        s_auth_ok = 1;
        return send_pkt(&ok, 1);
    }
    if (p[0] == SSH_CHANNEL_OPEN) {
        uint8_t conf[32];
        uint32_t cn = 0;
        uint32_t sender;
        uint32_t win;
        uint32_t maxp;
        off = 1;
        if (take_str(p, n, &off, &sv, &svn) != 0 || off + 12u > n) {
            return 0;
        }
        sender = get_be32(p + off);
        win = get_be32(p + off + 4);
        maxp = get_be32(p + off + 8);
        s_remote_chan = sender;
        s_chan_ok = 1;
        conf[cn++] = SSH_CHANNEL_OPEN_CONF;
        put_be32(conf + cn, sender);
        cn += 4;
        put_be32(conf + cn, 0);
        cn += 4;
        put_be32(conf + cn, win);
        cn += 4;
        put_be32(conf + cn, maxp);
        cn += 4;
        return send_pkt(conf, cn);
    }
    if (p[0] == SSH_CHANNEL_REQUEST) {
        uint8_t ok[8];
        uint32_t want;
        if (n < 10u) {
            return 0;
        }
        off = 5;
        if (take_str(p, n, &off, &sv, &svn) != 0 || off >= n) {
            return 0;
        }
        want = p[off];
        if (want != 0u) {
            ok[0] = SSH_CHANNEL_SUCCESS;
            put_be32(ok + 1, s_remote_chan);
            return send_pkt(ok, 5);
        }
        return 0;
    }
    if (p[0] == SSH_CHANNEL_DATA) {
        const uint8_t *data = NULL;
        uint32_t dn = 0;
        if (n < 9u) {
            return 0;
        }
        off = 5;
        if (take_str(p, n, &off, &data, &dn) != 0 || dn == 0u) {
            return 0;
        }
        if (s_cons >= 1) {
            (void)pm_metal_console_write_id(s_cons, (const char *)data, dn);
        }
        return 0;
    }
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
    if (s_kex_ok) {
        return on_session(p, n);
    }
    return 0;
}

static int32_t take_clear(void) {
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
    s_seq_in++;
    return 1;
}

static int32_t take_enc(void) {
    uint8_t peek[SSH_BLOCK];
    uint8_t mac[SSH_MAC];
    uint8_t got[SSH_MAC];
    uint8_t ctr[SSH_BLOCK];
    uint8_t sb[SSH_BLOCK];
    size_t off;
    uint32_t plen;
    uint32_t pad;
    uint32_t pay;
    uint32_t wire;
    if (s_rx_len < SSH_BLOCK + SSH_MAC) {
        return 0;
    }
    memcpy(ctr, s_ctr_in, SSH_BLOCK);
    memcpy(sb, s_sb_in, SSH_BLOCK);
    off = s_off_in;
    if (mbedtls_aes_crypt_ctr(&s_aes_in, SSH_BLOCK, &off, ctr, sb, s_rx, peek) != 0) {
        return -1;
    }
    plen = get_be32(peek);
    if (plen < SSH_BLOCK || plen > PAY_MAX) {
        return -1;
    }
    wire = 4u + plen;
    if (s_rx_len < wire + SSH_MAC) {
        return 0;
    }
    if (mbedtls_aes_crypt_ctr(&s_aes_in, wire, &s_off_in, s_ctr_in, s_sb_in, s_rx, s_rx) != 0) {
        return -1;
    }
    pad = s_rx[4];
    if (pad < 4u || 1u + pad >= plen) {
        return -1;
    }
    if (hmac_pkt(s_mk_in, s_seq_in, s_rx, wire, mac) != 0) {
        return -1;
    }
    memcpy(got, s_rx + wire, SSH_MAC);
    if (memcmp(mac, got, SSH_MAC) != 0) {
        return -1;
    }
    pay = plen - 1u - pad;
    if (on_payload(s_rx + 5, pay) != 0) {
        return -1;
    }
    memmove(s_rx, s_rx + wire + SSH_MAC, s_rx_len - (wire + SSH_MAC));
    s_rx_len -= wire + SSH_MAC;
    s_seq_in++;
    return 1;
}

static int32_t pump_rx(void) {
    for (;;) {
        int32_t st;
        if (s_enc_in) {
            st = take_enc();
        } else {
            st = take_clear();
        }
        if (st <= 0) {
            return st;
        }
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

static void reset_peer(void) {
    s_kex_ok = 0;
    s_auth_ok = 0;
    s_chan_ok = 0;
    s_remote_chan = 0;
    s_got_ident = 0;
    s_sent_kex = 0;
    s_got_ic = 0;
    s_rx_len = 0;
    s_ic_len = 0;
    s_seq_in = 0;
    s_seq_out = 0;
    s_enc_in = 0;
    s_enc_out = 0;
    s_keys_ok = 0;
    s_k_mp_len = 0;
}

int32_t pm_metal_net_ssh_init(pm_util_mem_arena_t *arena) {
    static const char pers[] = "metal-ssh";
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_ls = -1;
    s_peer = -1;
    s_have_key = 0;
    s_is_len = 0;
    s_cons = -1;
    reset_peer();
    memcpy(s_vc, s_vc_default, sizeof(s_vc_default) - 1u);
    s_vc_len = (uint32_t)(sizeof(s_vc_default) - 1u);
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_drbg);
    mbedtls_aes_init(&s_aes_in);
    mbedtls_aes_init(&s_aes_out);
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
    mbedtls_aes_free(&s_aes_in);
    mbedtls_aes_free(&s_aes_out);
    mbedtls_ctr_drbg_free(&s_drbg);
    mbedtls_entropy_free(&s_entropy);
    reset_peer();
    s_cons = -1;
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
        reset_peer();
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

int32_t pm_metal_net_ssh_auth_ok(void) {
    return s_auth_ok ? 1 : 0;
}

int32_t pm_metal_net_ssh_channel_ok(void) {
    return s_chan_ok ? 1 : 0;
}

int32_t pm_metal_net_ssh_console_id(void) {
    return s_cons;
}

int32_t pm_metal_net_ssh_viewport_attach(int32_t id) {
    if (id < 1 || id >= SSH_CONSOLE_N) {
        return -1;
    }
    if (s_cons == id) {
        return 0;
    }
    s_cons = id;
    return pm_metal_console_viewport_attach_id(id, "ssh", ssh_sink);
}

int32_t pm_metal_net_ssh_up(void) {
    if (s_arena == NULL || pm_metal_net_ssh_ident() == NULL) {
        return -1;
    }
    if (pm_metal_net_ssh_viewport_attach(0) == 0) {
        return -1;
    }
    if (pm_metal_net_ssh_viewport_attach(1) != 0 || s_cons != 1) {
        return -1;
    }
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_init, pm_metal_net_ssh_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_deinit, pm_metal_net_ssh_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_listen, pm_metal_net_ssh_listen, int32_t(uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_poll, pm_metal_net_ssh_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_ident, pm_metal_net_ssh_ident, const char *(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_kex_ok, pm_metal_net_ssh_kex_ok, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_auth_ok, pm_metal_net_ssh_auth_ok, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_channel_ok, pm_metal_net_ssh_channel_ok, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_console_id, pm_metal_net_ssh_console_id, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_viewport_attach, pm_metal_net_ssh_viewport_attach, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_up, pm_metal_net_ssh_up, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_init, pm_metal_net_ssh_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ssh, pymergetic.metal.net.ip);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ssh, pymergetic.metal.console);
