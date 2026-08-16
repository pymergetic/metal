/* pymergetic.metal.net.wg — Noise_IK (X25519 + ChaCha20-Poly1305, BLAKE2s HKDF).
 * peer(id, local, remote) takes 32-byte static secrets; pubs are X25519(secret, 9). */
#include "pymergetic/metal/net/wg/__exports__.h"
#include "pymergetic/metal/net/wg/__crypto__.h"

#include "pymergetic/metal/net/ip.h"

#include <string.h>

#define WG_KEY 32
#define WG_TAG 16
#define WG_NONCE 8
#define WG_QMAX 4096

struct wg_peer {
    uint32_t used;
    uint8_t local[WG_KEY];
    uint8_t remote[WG_KEY];
};

typedef struct {
    uint8_t ck[32];
    uint8_t h[32];
    uint8_t k[32];
    uint64_t n;
} noise_st;

static pm_util_mem_arena_t *s_arena;
static struct wg_peer s_peer[2];
static uint8_t s_sess[WG_KEY];
static uint32_t s_ready;
static uint64_t s_nonce;
static uint8_t s_q[WG_QMAX];
static uint16_t s_qlen;
static uint8_t s_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x05 };

static uint32_t mac_eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint8_t d = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        d = (uint8_t)(d | (a[i] ^ b[i]));
    }
    return d == 0;
}

static void noise_mixhash(noise_st *s, const uint8_t *data, uint32_t n) {
    uint8_t buf[32 + 64];
    if (n > 64) {
        n = 64;
    }
    memcpy(buf, s->h, 32);
    memcpy(buf + 32, data, n);
    pm_metal_wg_blake2s(buf, 32 + n, s->h);
}

static void noise_mixkey(noise_st *s, const uint8_t *ikm, uint32_t n) {
    pm_metal_wg_hkdf2(s->ck, ikm, n, s->ck, s->k);
    s->n = 0;
}

static int32_t noise_encrypt_hash(noise_st *s, const uint8_t *pt, uint32_t pt_len, uint8_t *ct) {
    uint8_t tag[WG_TAG];
    if (pm_metal_wg_aead_encrypt(s->k, s->n, s->h, 32, pt, pt_len, ct, tag) != 0) {
        return -1;
    }
    s->n++;
    memcpy(ct + pt_len, tag, WG_TAG);
    noise_mixhash(s, ct, pt_len + WG_TAG);
    return 0;
}

static int32_t noise_decrypt_hash(noise_st *s, const uint8_t *ct, uint32_t pt_len, uint8_t *pt) {
    const uint8_t *tag = ct + pt_len;
    if (pm_metal_wg_aead_decrypt(s->k, s->n, s->h, 32, ct, pt_len, tag, pt) != 0) {
        return -1;
    }
    s->n++;
    noise_mixhash(s, ct, pt_len + WG_TAG);
    return 0;
}

static void noise_init(noise_st *s) {
    static const uint8_t name[] = "Noise_IK_25519_ChaChaPoly_BLAKE2s";
    pm_metal_wg_blake2s(name, sizeof(name) - 1u, s->h);
    memcpy(s->ck, s->h, 32);
    memset(s->k, 0, 32);
    s->n = 0;
}

static void eph_from(const uint8_t *priv, const char *tag, uint8_t out[32]) {
    uint8_t buf[40];
    uint32_t n = 0;
    memcpy(buf, priv, 32);
    while (tag[n] != 0 && n < 8) {
        buf[32 + n] = (uint8_t)tag[n];
        n++;
    }
    pm_metal_wg_blake2s(buf, 32 + n, out);
}

static void put_be64(uint8_t *p, uint64_t v) {
    uint32_t i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (56u - 8u * i));
    }
}

static int32_t wg_open(void *ctx) {
    (void)ctx;
    if (!s_ready) {
        return -1;
    }
    s_qlen = 0;
    s_nonce = 0;
    return 0;
}

static void wg_mac(void *ctx, uint8_t out[6]) {
    (void)ctx;
    memcpy(out, s_mac, 6);
}

static int32_t wg_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    uint8_t tag[WG_TAG];
    (void)ctx;
    if (!s_ready || frame == NULL || len == 0) {
        return -1;
    }
    if ((uint32_t)len + WG_NONCE + WG_TAG > sizeof(s_q)) {
        return -1;
    }
    if (pm_metal_wg_aead_encrypt(s_sess, s_nonce, NULL, 0, frame, len, s_q + WG_NONCE, tag) != 0) {
        return -1;
    }
    put_be64(s_q, s_nonce);
    memcpy(s_q + WG_NONCE + len, tag, WG_TAG);
    s_qlen = (uint16_t)(WG_NONCE + len + WG_TAG);
    s_nonce++;
    return 0;
}

static int32_t wg_poll(void *ctx) {
    uint8_t body[WG_QMAX];
    uint8_t tag[WG_TAG];
    uint16_t n;
    uint64_t nonce = 0;
    uint32_t i;
    (void)ctx;
    if (s_qlen <= WG_NONCE + WG_TAG) {
        return 0;
    }
    n = (uint16_t)(s_qlen - WG_NONCE - WG_TAG);
    for (i = 0; i < 8; i++) {
        nonce = (nonce << 8) | s_q[i];
    }
    memcpy(tag, s_q + WG_NONCE + n, WG_TAG);
    s_qlen = 0;
    if (pm_metal_wg_aead_decrypt(s_sess, nonce, NULL, 0, s_q + WG_NONCE, n, tag, body) != 0) {
        return 0;
    }
    return pm_metal_net_ip_rx(body, n);
}

static const pm_metal_net_l2_ops_t s_ops = {
    .open = wg_open,
    .mac = wg_mac,
    .tx = wg_tx,
    .poll = wg_poll,
    .ctx = NULL,
};

int32_t pm_metal_net_wg_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_peer, 0, sizeof(s_peer));
    memset(s_sess, 0, sizeof(s_sess));
    s_ready = 0;
    s_qlen = 0;
    s_nonce = 0;
    return 0;
}

void pm_metal_net_wg_deinit(void) {
    memset(s_peer, 0, sizeof(s_peer));
    s_ready = 0;
    s_qlen = 0;
    s_arena = NULL;
}

int32_t pm_metal_net_wg_peer(int32_t id, const uint8_t *local, const uint8_t *remote) {
    if (s_arena == NULL || local == NULL || remote == NULL || id < 0 || id > 1) {
        return -1;
    }
    memcpy(s_peer[id].local, local, WG_KEY);
    memcpy(s_peer[id].remote, remote, WG_KEY);
    s_peer[id].used = 1;
    s_ready = 0;
    return 0;
}

int32_t pm_metal_net_wg_handshake(void) {
    uint8_t pub0[32], pub1[32], want0[32], want1[32];
    uint8_t e0[32], e1[32], e0pub[32], e1pub[32];
    uint8_t dh[32];
    uint8_t msg_s[32 + WG_TAG];
    uint8_t msg_p[WG_TAG];
    uint8_t msg_r[WG_TAG];
    uint8_t got[32];
    noise_st ini;
    noise_st rsp;
    uint8_t k2[32];
    if (!s_peer[0].used || !s_peer[1].used) {
        return -1;
    }
    pm_metal_wg_x25519_base(pub0, s_peer[0].local);
    pm_metal_wg_x25519_base(pub1, s_peer[1].local);
    pm_metal_wg_x25519_base(want1, s_peer[0].remote);
    pm_metal_wg_x25519_base(want0, s_peer[1].remote);
    if (!mac_eq(want1, pub1, 32) || !mac_eq(want0, pub0, 32)) {
        return -1;
    }
    eph_from(s_peer[0].local, "eph0", e0);
    eph_from(s_peer[1].local, "eph1", e1);
    pm_metal_wg_x25519_base(e0pub, e0);
    pm_metal_wg_x25519_base(e1pub, e1);

    /* Initiator: -> e, es, s, ss */
    noise_init(&ini);
    noise_mixhash(&ini, pub1, 32);
    noise_mixhash(&ini, e0pub, 32);
    if (pm_metal_wg_x25519(dh, e0, pub1) != 0) {
        return -1;
    }
    noise_mixkey(&ini, dh, 32);
    if (noise_encrypt_hash(&ini, pub0, 32, msg_s) != 0) {
        return -1;
    }
    if (pm_metal_wg_x25519(dh, s_peer[0].local, pub1) != 0) {
        return -1;
    }
    noise_mixkey(&ini, dh, 32);
    if (noise_encrypt_hash(&ini, NULL, 0, msg_p) != 0) {
        return -1;
    }

    /* Responder: decrypt s, ss, then <- e, ee, se */
    noise_init(&rsp);
    noise_mixhash(&rsp, pub1, 32);
    noise_mixhash(&rsp, e0pub, 32);
    if (pm_metal_wg_x25519(dh, s_peer[1].local, e0pub) != 0) {
        return -1;
    }
    noise_mixkey(&rsp, dh, 32);
    if (noise_decrypt_hash(&rsp, msg_s, 32, got) != 0 || !mac_eq(got, pub0, 32)) {
        return -1;
    }
    if (pm_metal_wg_x25519(dh, s_peer[1].local, pub0) != 0) {
        return -1;
    }
    noise_mixkey(&rsp, dh, 32);
    if (noise_decrypt_hash(&rsp, msg_p, 0, NULL) != 0) {
        return -1;
    }
    noise_mixhash(&rsp, e1pub, 32);
    if (pm_metal_wg_x25519(dh, e1, e0pub) != 0) {
        return -1;
    }
    noise_mixkey(&rsp, dh, 32);
    if (pm_metal_wg_x25519(dh, e1, pub0) != 0) {
        return -1;
    }
    noise_mixkey(&rsp, dh, 32);
    if (noise_encrypt_hash(&rsp, NULL, 0, msg_r) != 0) {
        return -1;
    }

    noise_mixhash(&ini, e1pub, 32);
    if (pm_metal_wg_x25519(dh, e0, e1pub) != 0) {
        return -1;
    }
    noise_mixkey(&ini, dh, 32);
    if (pm_metal_wg_x25519(dh, s_peer[0].local, e1pub) != 0) {
        return -1;
    }
    noise_mixkey(&ini, dh, 32);
    if (noise_decrypt_hash(&ini, msg_r, 0, NULL) != 0) {
        return -1;
    }
    if (!mac_eq(ini.ck, rsp.ck, 32)) {
        return -1;
    }
    pm_metal_wg_hkdf2(ini.ck, NULL, 0, s_sess, k2);
    (void)k2;
    s_ready = 1;
    return 0;
}

int32_t pm_metal_net_wg_up(void) {
    if (s_arena == NULL || !s_ready) {
        return -1;
    }
    return pm_metal_net_l2_attach("wg", &s_ops);
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.wg, pm_metal_net_wg_init, pm_metal_net_wg_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.wg, pm_metal_net_wg_deinit, pm_metal_net_wg_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.wg, pm_metal_net_wg_peer, pm_metal_net_wg_peer, int32_t(int32_t, const uint8_t *, const uint8_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.wg, pm_metal_net_wg_handshake, pm_metal_net_wg_handshake, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.wg, pm_metal_net_wg_up, pm_metal_net_wg_up, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.net.wg, pm_metal_net_wg_init, pm_metal_net_wg_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.wg, pymergetic.metal.net.ip);
