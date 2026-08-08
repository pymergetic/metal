#include "kex.h"

#include <string.h>

#include "crypto.h"
#include "monocypher.h"

#define SSH_MSG_KEXINIT 20
#define SSH_MSG_KEX_ECDH_INIT 30
#define SSH_MSG_KEX_ECDH_REPLY 31

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t put_name_list(uint8_t *p, uint32_t cap, const char *s)
{
    return pm_metal_net_ssh_put_cstring(p, cap, s);
}

void pm_metal_net_ssh_kex_reset(pm_metal_net_ssh_kex_t *k)
{
    if (k == NULL) {
        return;
    }
    memset(k, 0, sizeof(*k));
}

uint32_t pm_metal_net_ssh_kex_build_init(uint8_t *dst, uint32_t cap)
{
    uint32_t o = 0;
    uint32_t n;

    if (dst == NULL || cap < 64u) {
        return 0;
    }
    dst[o++] = SSH_MSG_KEXINIT;
    pm_metal_net_ssh_crypto_random(dst + o, 16);
    o += 16;
    /* Advertise strict-KEX so both sides reset seq after NEWKEYS (OpenSSH 9+). */
    n = put_name_list(dst + o, cap - o,
        "curve25519-sha256,kex-strict-s-v00@openssh.com");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = put_name_list(dst + o, cap - o, "ssh-ed25519");
    if (n == 0u) {
        return 0;
    }
    o += n;
    /* Prefer chacha for later; aes128-ctr kept as handshake-friendly alt. */
    n = put_name_list(dst + o, cap - o, "chacha20-poly1305@openssh.com,aes128-ctr");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = put_name_list(dst + o, cap - o, "chacha20-poly1305@openssh.com,aes128-ctr");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = put_name_list(dst + o, cap - o, "hmac-sha2-256");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = put_name_list(dst + o, cap - o, "hmac-sha2-256");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = put_name_list(dst + o, cap - o, "none");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = put_name_list(dst + o, cap - o, "none");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = put_name_list(dst + o, cap - o, "");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = put_name_list(dst + o, cap - o, "");
    if (n == 0u) {
        return 0;
    }
    o += n;
    if (o + 5u > cap) {
        return 0;
    }
    dst[o++] = 0; /* first_kex_packet_follows */
    put_u32(dst + o, 0);
    o += 4;
    return o;
}

static void hash_string(void *ctx, const uint8_t *s, uint32_t n)
{
    uint8_t len[4];

    put_u32(len, n);
    pm_metal_net_ssh_sha256_update(ctx, len, 4);
    if (n > 0u && s != NULL) {
        pm_metal_net_ssh_sha256_update(ctx, s, n);
    }
}

int32_t pm_metal_net_ssh_kex_server_reply(pm_metal_net_ssh_kex_t *k,
    const uint8_t *peer_init, uint32_t peer_init_len, uint8_t *reply,
    uint32_t reply_cap, uint32_t *reply_len)
{
    uint8_t q_c[32];
    uint8_t shared[32];
    uint8_t host_blob[128];
    uint8_t mp[40];
    uint8_t sig[64];
    uint8_t sig_blob[96];
    uint8_t H[32];
    uint8_t sha_ctx[128];
    uint32_t host_len;
    uint32_t mp_len;
    uint32_t sig_blob_len;
    uint32_t o;
    uint32_t n;
    uint32_t q_len;

    if (k == NULL || peer_init == NULL || reply == NULL || reply_len == NULL) {
        return -1;
    }
    *reply_len = 0;
    if (peer_init_len < 5u || peer_init[0] != SSH_MSG_KEX_ECDH_INIT) {
        return -1;
    }
    q_len = ((uint32_t)peer_init[1] << 24) | ((uint32_t)peer_init[2] << 16)
        | ((uint32_t)peer_init[3] << 8) | (uint32_t)peer_init[4];
    if (q_len != 32u || peer_init_len < 5u + 32u) {
        return -1;
    }
    memcpy(q_c, peer_init + 5, 32);

    if (pm_metal_net_ssh_sha256_ctx_size() > sizeof(sha_ctx)) {
        return -1;
    }

    pm_metal_net_ssh_x25519_keypair(k->eph_sk, k->eph_pk);
    pm_metal_net_ssh_x25519(shared, k->eph_sk, q_c);

    host_len = pm_metal_net_ssh_hostkey_blob(host_blob, sizeof(host_blob));
    if (host_len == 0u) {
        return -1;
    }
    mp_len = pm_metal_net_ssh_put_mpint(mp, sizeof(mp), shared, 32);
    if (mp_len == 0u) {
        return -1;
    }

    pm_metal_net_ssh_sha256_init(sha_ctx);
    hash_string(sha_ctx, k->v_c, k->v_c_len);
    hash_string(sha_ctx, k->v_s, k->v_s_len);
    hash_string(sha_ctx, k->i_c, k->i_c_len);
    hash_string(sha_ctx, k->i_s, k->i_s_len);
    hash_string(sha_ctx, host_blob, host_len);
    hash_string(sha_ctx, q_c, 32);
    hash_string(sha_ctx, k->eph_pk, 32);
    pm_metal_net_ssh_sha256_update(sha_ctx, mp, mp_len);
    pm_metal_net_ssh_sha256_final(sha_ctx, H);
    memcpy(k->session_id, H, 32);
    memcpy(k->H, H, 32);
    k->have_session = 1;
    if (mp_len > sizeof(k->K_mpint)) {
        return -1;
    }
    memcpy(k->K_mpint, mp, mp_len);
    k->K_mpint_len = mp_len;

    pm_metal_net_ssh_ed25519_sign(sig, H, 32);
    o = 0;
    n = pm_metal_net_ssh_put_cstring(sig_blob + o, sizeof(sig_blob) - o,
        "ssh-ed25519");
    if (n == 0u) {
        return -1;
    }
    o += n;
    n = pm_metal_net_ssh_put_string(sig_blob + o, sizeof(sig_blob) - o, sig, 64);
    if (n == 0u) {
        return -1;
    }
    sig_blob_len = o + n;

    o = 0;
    if (reply_cap < 8u) {
        return -1;
    }
    reply[o++] = SSH_MSG_KEX_ECDH_REPLY;
    n = pm_metal_net_ssh_put_string(reply + o, reply_cap - o, host_blob, host_len);
    if (n == 0u) {
        return -1;
    }
    o += n;
    n = pm_metal_net_ssh_put_string(reply + o, reply_cap - o, k->eph_pk, 32);
    if (n == 0u) {
        return -1;
    }
    o += n;
    n = pm_metal_net_ssh_put_string(reply + o, reply_cap - o, sig_blob,
        sig_blob_len);
    if (n == 0u) {
        return -1;
    }
    o += n;
    *reply_len = o;
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(k->eph_sk, sizeof(k->eph_sk));
    return 0;
}

/* K1 = HASH(K || H || X || session_id); Kn = HASH(K || H || K1..Kn-1). */
static void expand64(pm_metal_net_ssh_kex_t *k, uint8_t letter, uint8_t out[64])
{
    uint8_t ctx[128];

    pm_metal_net_ssh_sha256_init(ctx);
    pm_metal_net_ssh_sha256_update(ctx, k->K_mpint, k->K_mpint_len);
    pm_metal_net_ssh_sha256_update(ctx, k->H, 32);
    pm_metal_net_ssh_sha256_update(ctx, &letter, 1);
    pm_metal_net_ssh_sha256_update(ctx, k->session_id, 32);
    pm_metal_net_ssh_sha256_final(ctx, out);

    pm_metal_net_ssh_sha256_init(ctx);
    pm_metal_net_ssh_sha256_update(ctx, k->K_mpint, k->K_mpint_len);
    pm_metal_net_ssh_sha256_update(ctx, k->H, 32);
    pm_metal_net_ssh_sha256_update(ctx, out, 32);
    pm_metal_net_ssh_sha256_final(ctx, out + 32);
}

int32_t pm_metal_net_ssh_kex_derive_keys(pm_metal_net_ssh_kex_t *k)
{
    if (k == NULL || !k->have_session || k->K_mpint_len == 0u) {
        return -1;
    }
    /* C = client→server enc, D = server→client enc (64 bytes for ChaCha). */
    expand64(k, (uint8_t)'C', k->key_c2s);
    expand64(k, (uint8_t)'D', k->key_s2c);
    k->have_keys = 1;
    return 0;
}
