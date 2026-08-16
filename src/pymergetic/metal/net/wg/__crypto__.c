/* pymergetic.metal.net.wg — compact X25519 (mbedtls MPI) + ChaCha20-Poly1305 + BLAKE2s.
 * µPy mbedtls config has no CURVE25519 / CHACHAPOLY / BLAKE2s — those are in-card. */
#include "pymergetic/metal/net/wg/__crypto__.h"

#include "mbedtls/bignum.h"

#include <string.h>

static uint32_t load32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void store64_le(uint8_t *p, uint64_t v) {
    store32_le(p, (uint32_t)v);
    store32_le(p + 4, (uint32_t)(v >> 32));
}

#define BLAKE2S_IV0 0x6A09E667u
#define BLAKE2S_IV1 0xBB67AE85u
#define BLAKE2S_IV2 0x3C6EF372u
#define BLAKE2S_IV3 0xA54FF53Au
#define BLAKE2S_IV4 0x510E527Fu
#define BLAKE2S_IV5 0x9B05688Cu
#define BLAKE2S_IV6 0x1F83D9ABu
#define BLAKE2S_IV7 0x5BE0CD19u

static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static const uint8_t blake2s_sigma[10][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
};

#define B2S_G(a, b, c, d, x, y) \
    do { \
        v[a] = v[a] + v[b] + (x); \
        v[d] = rotr32(v[d] ^ v[a], 16); \
        v[c] = v[c] + v[d]; \
        v[b] = rotr32(v[b] ^ v[c], 12); \
        v[a] = v[a] + v[b] + (y); \
        v[d] = rotr32(v[d] ^ v[a], 8); \
        v[c] = v[c] + v[d]; \
        v[b] = rotr32(v[b] ^ v[c], 7); \
    } while (0)

static void blake2s_compress(uint32_t h[8], const uint8_t block[64], uint64_t t, int last) {
    uint32_t m[16];
    uint32_t v[16];
    uint32_t i;
    uint32_t r;
    for (i = 0; i < 16; i++) {
        m[i] = load32_le(block + 4u * i);
    }
    for (i = 0; i < 8; i++) {
        v[i] = h[i];
    }
    v[8] = BLAKE2S_IV0;
    v[9] = BLAKE2S_IV1;
    v[10] = BLAKE2S_IV2;
    v[11] = BLAKE2S_IV3;
    v[12] = BLAKE2S_IV4 ^ (uint32_t)t;
    v[13] = BLAKE2S_IV5 ^ (uint32_t)(t >> 32);
    v[14] = BLAKE2S_IV6;
    v[15] = BLAKE2S_IV7;
    if (last) {
        v[14] = ~v[14];
    }
    for (r = 0; r < 10; r++) {
        const uint8_t *s = blake2s_sigma[r];
        B2S_G(0, 4, 8, 12, m[s[0]], m[s[1]]);
        B2S_G(1, 5, 9, 13, m[s[2]], m[s[3]]);
        B2S_G(2, 6, 10, 14, m[s[4]], m[s[5]]);
        B2S_G(3, 7, 11, 15, m[s[6]], m[s[7]]);
        B2S_G(0, 5, 10, 15, m[s[8]], m[s[9]]);
        B2S_G(1, 6, 11, 12, m[s[10]], m[s[11]]);
        B2S_G(2, 7, 8, 13, m[s[12]], m[s[13]]);
        B2S_G(3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for (i = 0; i < 8; i++) {
        h[i] ^= v[i] ^ v[i + 8];
    }
}

void pm_metal_wg_blake2s(const uint8_t *in, uint32_t n, uint8_t out[32]) {
    uint32_t h[8];
    uint8_t block[64];
    uint64_t t = 0;
    uint32_t i;
    h[0] = BLAKE2S_IV0 ^ 0x01010020u; /* param: digest 32, fanout/depth 1 */
    h[1] = BLAKE2S_IV1;
    h[2] = BLAKE2S_IV2;
    h[3] = BLAKE2S_IV3;
    h[4] = BLAKE2S_IV4;
    h[5] = BLAKE2S_IV5;
    h[6] = BLAKE2S_IV6;
    h[7] = BLAKE2S_IV7;
    while (n > 64) {
        t += 64;
        blake2s_compress(h, in, t, 0);
        in += 64;
        n -= 64;
    }
    memset(block, 0, 64);
    if (n != 0 && in != NULL) {
        memcpy(block, in, n);
    }
    t += n;
    blake2s_compress(h, block, t, 1);
    for (i = 0; i < 8; i++) {
        store32_le(out + 4u * i, h[i]);
    }
}

static void hmac_blake2s(const uint8_t *key, uint32_t klen, const uint8_t *in, uint32_t n, uint8_t out[32]) {
    uint8_t kpad[64];
    uint8_t inner[64 + 256];
    uint8_t outer[64 + 32];
    uint8_t kh[32];
    uint32_t i;
    if (klen > 64) {
        pm_metal_wg_blake2s(key, klen, kh);
        key = kh;
        klen = 32;
    }
    memset(kpad, 0, 64);
    memcpy(kpad, key, klen);
    for (i = 0; i < 64; i++) {
        inner[i] = (uint8_t)(kpad[i] ^ 0x36u);
        outer[i] = (uint8_t)(kpad[i] ^ 0x5cu);
    }
    if (n > 256) {
        n = 256;
    }
    if (n != 0 && in != NULL) {
        memcpy(inner + 64, in, n);
    }
    pm_metal_wg_blake2s(inner, 64 + n, outer + 64);
    pm_metal_wg_blake2s(outer, 96, out);
}

void pm_metal_wg_hkdf2(const uint8_t ck[32], const uint8_t *ikm, uint32_t ikm_len, uint8_t out1[32],
    uint8_t out2[32]) {
    uint8_t prk[32];
    uint8_t tmp[33];
    uint8_t empty = 0;
    if (ikm == NULL) {
        ikm = &empty;
        ikm_len = 0;
    }
    hmac_blake2s(ck, 32, ikm, ikm_len, prk);
    tmp[0] = 1;
    hmac_blake2s(prk, 32, tmp, 1, out1);
    memcpy(tmp, out1, 32);
    tmp[32] = 2;
    hmac_blake2s(prk, 32, tmp, 33, out2);
}

static int mpi_from_le32(mbedtls_mpi *x, const uint8_t *le) {
    uint8_t be[32];
    uint32_t i;
    for (i = 0; i < 32; i++) {
        be[i] = le[31u - i];
    }
    return mbedtls_mpi_read_binary(x, be, 32);
}

static int mpi_to_le32(uint8_t *le, const mbedtls_mpi *x) {
    uint8_t be[32];
    uint32_t i;
    int r = mbedtls_mpi_write_binary(x, be, 32);
    if (r != 0) {
        return r;
    }
    for (i = 0; i < 32; i++) {
        le[i] = be[31u - i];
    }
    return 0;
}

static int mpi_add_mod(mbedtls_mpi *o, const mbedtls_mpi *a, const mbedtls_mpi *b, const mbedtls_mpi *p) {
    mbedtls_mpi tmp;
    int r;
    mbedtls_mpi_init(&tmp);
    r = mbedtls_mpi_add_mpi(&tmp, a, b);
    if (r == 0) {
        r = mbedtls_mpi_mod_mpi(o, &tmp, p);
    }
    mbedtls_mpi_free(&tmp);
    return r;
}

static int mpi_sub_mod(mbedtls_mpi *o, const mbedtls_mpi *a, const mbedtls_mpi *b, const mbedtls_mpi *p) {
    mbedtls_mpi tmp;
    int r;
    mbedtls_mpi_init(&tmp);
    r = mbedtls_mpi_sub_mpi(&tmp, a, b);
    if (r == 0) {
        while (mbedtls_mpi_cmp_int(&tmp, 0) < 0) {
            r = mbedtls_mpi_add_mpi(&tmp, &tmp, p);
            if (r != 0) {
                break;
            }
        }
    }
    if (r == 0) {
        r = mbedtls_mpi_mod_mpi(o, &tmp, p);
    }
    mbedtls_mpi_free(&tmp);
    return r;
}

static int mpi_mul_mod(mbedtls_mpi *o, const mbedtls_mpi *a, const mbedtls_mpi *b, const mbedtls_mpi *p) {
    mbedtls_mpi tmp;
    int r;
    mbedtls_mpi_init(&tmp);
    r = mbedtls_mpi_mul_mpi(&tmp, a, b);
    if (r == 0) {
        r = mbedtls_mpi_mod_mpi(o, &tmp, p);
    }
    mbedtls_mpi_free(&tmp);
    return r;
}

static int mpi_sq_mod(mbedtls_mpi *o, const mbedtls_mpi *a, const mbedtls_mpi *p) {
    mbedtls_mpi tmp;
    int r;
    mbedtls_mpi_init(&tmp);
    r = mbedtls_mpi_mul_mpi(&tmp, a, a);
    if (r == 0) {
        r = mbedtls_mpi_mod_mpi(o, &tmp, p);
    }
    mbedtls_mpi_free(&tmp);
    return r;
}

int32_t pm_metal_wg_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]) {
    mbedtls_mpi P, a24, x2, z2, x3, z3, x1, A, AA, B, BB, E, C, D, DA, CB, t, s;
    uint8_t k[32];
    int i;
    int swap = 0;
    int r = 0;
    memcpy(k, scalar, 32);
    k[0] = (uint8_t)(k[0] & 248u);
    k[31] = (uint8_t)((k[31] & 127u) | 64u);
    mbedtls_mpi_init(&P);
    mbedtls_mpi_init(&a24);
    mbedtls_mpi_init(&x2);
    mbedtls_mpi_init(&z2);
    mbedtls_mpi_init(&x3);
    mbedtls_mpi_init(&z3);
    mbedtls_mpi_init(&x1);
    mbedtls_mpi_init(&A);
    mbedtls_mpi_init(&AA);
    mbedtls_mpi_init(&B);
    mbedtls_mpi_init(&BB);
    mbedtls_mpi_init(&E);
    mbedtls_mpi_init(&C);
    mbedtls_mpi_init(&D);
    mbedtls_mpi_init(&DA);
    mbedtls_mpi_init(&CB);
    mbedtls_mpi_init(&t);
    mbedtls_mpi_init(&s);
    r = mbedtls_mpi_lset(&P, 1);
    if (r == 0) {
        r = mbedtls_mpi_shift_l(&P, 255);
    }
    if (r == 0) {
        r = mbedtls_mpi_sub_int(&P, &P, 19);
    }
    if (r == 0) {
        r = mbedtls_mpi_lset(&a24, 121665);
    }
    if (r == 0) {
        uint8_t u_bits[32];
        memcpy(u_bits, u, 32);
        u_bits[31] = (uint8_t)(u_bits[31] & 127u);
        r = mpi_from_le32(&x1, u_bits);
    }
    if (r == 0) {
        r = mbedtls_mpi_mod_mpi(&x1, &x1, &P);
    }
    if (r == 0) {
        r = mbedtls_mpi_lset(&x2, 1);
    }
    if (r == 0) {
        r = mbedtls_mpi_lset(&z2, 0);
    }
    if (r == 0) {
        r = mbedtls_mpi_copy(&x3, &x1);
    }
    if (r == 0) {
        r = mbedtls_mpi_lset(&z3, 1);
    }
    for (i = 254; r == 0 && i >= 0; i--) {
        int kt = (k[i / 8] >> (i & 7)) & 1;
        swap ^= kt;
        if (swap) {
            mbedtls_mpi_swap(&x2, &x3);
            mbedtls_mpi_swap(&z2, &z3);
        }
        swap = kt;
        r = mpi_add_mod(&A, &x2, &z2, &P);
        if (r == 0) {
            r = mpi_sq_mod(&AA, &A, &P);
        }
        if (r == 0) {
            r = mpi_sub_mod(&B, &x2, &z2, &P);
        }
        if (r == 0) {
            r = mpi_sq_mod(&BB, &B, &P);
        }
        if (r == 0) {
            r = mpi_sub_mod(&E, &AA, &BB, &P);
        }
        if (r == 0) {
            r = mpi_add_mod(&C, &x3, &z3, &P);
        }
        if (r == 0) {
            r = mpi_sub_mod(&D, &x3, &z3, &P);
        }
        if (r == 0) {
            r = mpi_mul_mod(&DA, &D, &A, &P);
        }
        if (r == 0) {
            r = mpi_mul_mod(&CB, &C, &B, &P);
        }
        if (r == 0) {
            r = mpi_add_mod(&t, &DA, &CB, &P);
        }
        if (r == 0) {
            r = mpi_sq_mod(&x3, &t, &P);
        }
        if (r == 0) {
            r = mpi_sub_mod(&t, &DA, &CB, &P);
        }
        if (r == 0) {
            r = mpi_sq_mod(&s, &t, &P);
        }
        if (r == 0) {
            r = mpi_mul_mod(&z3, &x1, &s, &P);
        }
        if (r == 0) {
            r = mpi_mul_mod(&x2, &AA, &BB, &P);
        }
        if (r == 0) {
            r = mpi_mul_mod(&t, &a24, &E, &P);
        }
        if (r == 0) {
            r = mpi_add_mod(&s, &AA, &t, &P);
        }
        if (r == 0) {
            r = mpi_mul_mod(&z2, &E, &s, &P);
        }
    }
    if (r == 0 && swap) {
        mbedtls_mpi_swap(&x2, &x3);
        mbedtls_mpi_swap(&z2, &z3);
    }
    if (r == 0) {
        r = mbedtls_mpi_inv_mod(&t, &z2, &P);
    }
    if (r == 0) {
        r = mpi_mul_mod(&x2, &x2, &t, &P);
    }
    if (r == 0) {
        r = mpi_to_le32(out, &x2);
    }
    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&a24);
    mbedtls_mpi_free(&x2);
    mbedtls_mpi_free(&z2);
    mbedtls_mpi_free(&x3);
    mbedtls_mpi_free(&z3);
    mbedtls_mpi_free(&x1);
    mbedtls_mpi_free(&A);
    mbedtls_mpi_free(&AA);
    mbedtls_mpi_free(&B);
    mbedtls_mpi_free(&BB);
    mbedtls_mpi_free(&E);
    mbedtls_mpi_free(&C);
    mbedtls_mpi_free(&D);
    mbedtls_mpi_free(&DA);
    mbedtls_mpi_free(&CB);
    mbedtls_mpi_free(&t);
    mbedtls_mpi_free(&s);
    return r == 0 ? 0 : -1;
}

void pm_metal_wg_x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    uint8_t nine[32];
    memset(nine, 0, sizeof(nine));
    nine[0] = 9;
    (void)pm_metal_wg_x25519(out, scalar, nine);
}

static uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void chacha_qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b;
    *d = rotl32(*d ^ *a, 16);
    *c += *d;
    *b = rotl32(*b ^ *c, 12);
    *a += *b;
    *d = rotl32(*d ^ *a, 8);
    *c += *d;
    *b = rotl32(*b ^ *c, 7);
}

static void chacha20_block(uint32_t st[16], uint8_t out[64]) {
    uint32_t w[16];
    uint32_t i;
    memcpy(w, st, sizeof(w));
    for (i = 0; i < 10; i++) {
        chacha_qr(&w[0], &w[4], &w[8], &w[12]);
        chacha_qr(&w[1], &w[5], &w[9], &w[13]);
        chacha_qr(&w[2], &w[6], &w[10], &w[14]);
        chacha_qr(&w[3], &w[7], &w[11], &w[15]);
        chacha_qr(&w[0], &w[5], &w[10], &w[15]);
        chacha_qr(&w[1], &w[6], &w[11], &w[12]);
        chacha_qr(&w[2], &w[7], &w[8], &w[13]);
        chacha_qr(&w[3], &w[4], &w[9], &w[14]);
    }
    for (i = 0; i < 16; i++) {
        store32_le(out + 4u * i, w[i] + st[i]);
    }
}

static void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t *in,
    uint32_t n, uint8_t *out) {
    uint32_t st[16];
    uint32_t off = 0;
    st[0] = 0x61707865u;
    st[1] = 0x3320646eu;
    st[2] = 0x79622d32u;
    st[3] = 0x6b206574u;
    st[4] = load32_le(key);
    st[5] = load32_le(key + 4);
    st[6] = load32_le(key + 8);
    st[7] = load32_le(key + 12);
    st[8] = load32_le(key + 16);
    st[9] = load32_le(key + 20);
    st[10] = load32_le(key + 24);
    st[11] = load32_le(key + 28);
    st[13] = load32_le(nonce);
    st[14] = load32_le(nonce + 4);
    st[15] = load32_le(nonce + 8);
    while (off < n) {
        uint8_t blk[64];
        uint32_t i;
        uint32_t take = n - off;
        st[12] = counter;
        chacha20_block(st, blk);
        if (take > 64) {
            take = 64;
        }
        for (i = 0; i < take; i++) {
            out[off + i] = (uint8_t)(in[off + i] ^ blk[i]);
        }
        off += take;
        counter++;
    }
}

static void poly1305_mac(const uint8_t key[32], const uint8_t *ad, uint32_t ad_len, const uint8_t *ct, uint32_t ct_len,
    uint8_t tag[16]) {
    /* 5x26-bit limbs. */
    uint32_t r0, r1, r2, r3, r4;
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    uint32_t pad[4];
    uint8_t block[16];
    uint8_t lens[16];
    uint32_t pass;

    r0 = load32_le(key) & 0x3ffffffu;
    r1 = (load32_le(key + 3) >> 2) & 0x3ffff03u;
    r2 = (load32_le(key + 6) >> 4) & 0x3ffc0ffu;
    r3 = (load32_le(key + 9) >> 6) & 0x3f03fffu;
    r4 = (load32_le(key + 12) >> 8) & 0x00fffffu;
    pad[0] = load32_le(key + 16);
    pad[1] = load32_le(key + 20);
    pad[2] = load32_le(key + 24);
    pad[3] = load32_le(key + 28);

    for (pass = 0; pass < 3; pass++) {
        const uint8_t *msg;
        uint32_t msg_len;
        uint32_t off = 0;
        if (pass == 0) {
            msg = ad;
            msg_len = ad_len;
        } else if (pass == 1) {
            msg = ct;
            msg_len = ct_len;
        } else {
            memset(lens, 0, sizeof(lens));
            store64_le(lens, ad_len);
            store64_le(lens + 8, ct_len);
            msg = lens;
            msg_len = 16;
        }
        while (off < msg_len || pass == 2) {
            uint32_t n;
            uint64_t d0, d1, d2, d3, d4;
            uint64_t c;
            uint32_t hibit;
            if (pass == 2) {
                if (off != 0) {
                    break;
                }
                n = 16;
                hibit = 1u << 24;
                memcpy(block, msg, 16);
                off = 16;
            } else {
                if (off >= msg_len) {
                    break;
                }
                n = msg_len - off;
                hibit = 1u << 24;
                if (n > 16) {
                    n = 16;
                }
                memset(block, 0, 16);
                memcpy(block, msg + off, n);
                if (n < 16) {
                    block[n] = 1;
                    hibit = 0;
                }
                off += n;
            }
            h0 += load32_le(block) & 0x3ffffffu;
            h1 += (load32_le(block + 3) >> 2) & 0x3ffffffu;
            h2 += (load32_le(block + 6) >> 4) & 0x3ffffffu;
            h3 += (load32_le(block + 9) >> 6) & 0x3ffffffu;
            h4 += (load32_le(block + 12) >> 8) | hibit;
            d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * 5u * r4 + (uint64_t)h2 * 5u * r3 + (uint64_t)h3 * 5u * r2
                + (uint64_t)h4 * 5u * r1;
            d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * 5u * r4 + (uint64_t)h3 * 5u * r3
                + (uint64_t)h4 * 5u * r2;
            d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * 5u * r4
                + (uint64_t)h4 * 5u * r3;
            d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * 5u * r4;
            d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
            c = d0 >> 26;
            h0 = (uint32_t)d0 & 0x3ffffffu;
            d1 += c;
            c = d1 >> 26;
            h1 = (uint32_t)d1 & 0x3ffffffu;
            d2 += c;
            c = d2 >> 26;
            h2 = (uint32_t)d2 & 0x3ffffffu;
            d3 += c;
            c = d3 >> 26;
            h3 = (uint32_t)d3 & 0x3ffffffu;
            d4 += c;
            c = d4 >> 26;
            h4 = (uint32_t)d4 & 0x3ffffffu;
            h0 += (uint32_t)c * 5u;
            c = h0 >> 26;
            h0 &= 0x3ffffffu;
            h1 += (uint32_t)c;
        }
    }

    {
        uint64_t f;
        uint32_t g0, g1, g2, g3, g4, mask;
        uint32_t s0, s1, s2, s3;
        h1 += h0 >> 26;
        h0 &= 0x3ffffffu;
        h2 += h1 >> 26;
        h1 &= 0x3ffffffu;
        h3 += h2 >> 26;
        h2 &= 0x3ffffffu;
        h4 += h3 >> 26;
        h3 &= 0x3ffffffu;
        h0 += 5u * (h4 >> 26);
        h4 &= 0x3ffffffu;
        h1 += h0 >> 26;
        h0 &= 0x3ffffffu;
        g0 = h0 + 5u;
        g1 = h1 + (g0 >> 26);
        g0 &= 0x3ffffffu;
        g2 = h2 + (g1 >> 26);
        g1 &= 0x3ffffffu;
        g3 = h3 + (g2 >> 26);
        g2 &= 0x3ffffffu;
        g4 = h4 + (g3 >> 26);
        g3 &= 0x3ffffffu;
        mask = (g4 >> 26) - 1u;
        g4 &= 0x3ffffffu;
        h0 = (h0 & mask) | (g0 & ~mask);
        h1 = (h1 & mask) | (g1 & ~mask);
        h2 = (h2 & mask) | (g2 & ~mask);
        h3 = (h3 & mask) | (g3 & ~mask);
        h4 = (h4 & mask) | (g4 & ~mask);
        s0 = h0 | (h1 << 26);
        s1 = (h1 >> 6) | (h2 << 20);
        s2 = (h2 >> 12) | (h3 << 14);
        s3 = (h3 >> 18) | (h4 << 8);
        f = (uint64_t)s0 + pad[0];
        store32_le(tag, (uint32_t)f);
        f = (uint64_t)s1 + pad[1] + (f >> 32);
        store32_le(tag + 4, (uint32_t)f);
        f = (uint64_t)s2 + pad[2] + (f >> 32);
        store32_le(tag + 8, (uint32_t)f);
        f = (uint64_t)s3 + pad[3] + (f >> 32);
        store32_le(tag + 12, (uint32_t)f);
    }
}

static void noise_nonce(uint8_t n12[12], uint64_t n) {
    memset(n12, 0, 4);
    store64_le(n12 + 4, n);
}

int32_t pm_metal_wg_aead_encrypt(const uint8_t key[32], uint64_t nonce, const uint8_t *ad, uint32_t ad_len,
    const uint8_t *pt, uint32_t pt_len, uint8_t *ct, uint8_t tag[16]) {
    uint8_t n12[12];
    uint8_t poly_key[64];
    uint32_t st[16];
    noise_nonce(n12, nonce);
    st[0] = 0x61707865u;
    st[1] = 0x3320646eu;
    st[2] = 0x79622d32u;
    st[3] = 0x6b206574u;
    st[4] = load32_le(key);
    st[5] = load32_le(key + 4);
    st[6] = load32_le(key + 8);
    st[7] = load32_le(key + 12);
    st[8] = load32_le(key + 16);
    st[9] = load32_le(key + 20);
    st[10] = load32_le(key + 24);
    st[11] = load32_le(key + 28);
    st[12] = 0;
    st[13] = load32_le(n12);
    st[14] = load32_le(n12 + 4);
    st[15] = load32_le(n12 + 8);
    chacha20_block(st, poly_key);
    if (pt_len != 0 && (pt == NULL || ct == NULL)) {
        return -1;
    }
    if (pt_len != 0) {
        chacha20_xor(key, n12, 1, pt, pt_len, ct);
    }
    poly1305_mac(poly_key, ad, ad_len, ct, pt_len, tag);
    return 0;
}

int32_t pm_metal_wg_aead_decrypt(const uint8_t key[32], uint64_t nonce, const uint8_t *ad, uint32_t ad_len,
    const uint8_t *ct, uint32_t ct_len, const uint8_t tag[16], uint8_t *pt) {
    uint8_t expect[16];
    uint8_t n12[12];
    uint8_t poly_key[64];
    uint32_t st[16];
    uint8_t d = 0;
    uint32_t i;
    noise_nonce(n12, nonce);
    st[0] = 0x61707865u;
    st[1] = 0x3320646eu;
    st[2] = 0x79622d32u;
    st[3] = 0x6b206574u;
    st[4] = load32_le(key);
    st[5] = load32_le(key + 4);
    st[6] = load32_le(key + 8);
    st[7] = load32_le(key + 12);
    st[8] = load32_le(key + 16);
    st[9] = load32_le(key + 20);
    st[10] = load32_le(key + 24);
    st[11] = load32_le(key + 28);
    st[12] = 0;
    st[13] = load32_le(n12);
    st[14] = load32_le(n12 + 4);
    st[15] = load32_le(n12 + 8);
    chacha20_block(st, poly_key);
    poly1305_mac(poly_key, ad, ad_len, ct, ct_len, expect);
    for (i = 0; i < 16; i++) {
        d = (uint8_t)(d | (expect[i] ^ tag[i]));
    }
    if (d != 0) {
        return -1;
    }
    if (ct_len != 0 && (ct == NULL || pt == NULL)) {
        return -1;
    }
    if (ct_len != 0) {
        chacha20_xor(key, n12, 1, ct, ct_len, pt);
    }
    return 0;
}

int32_t pm_metal_wg_crypto_selftest(void) {
    /* RFC 7748 §6.1 Alice public from private. */
    static const uint8_t alice_priv[32] = {
        0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
        0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a,
    };
    static const uint8_t alice_pub[32] = {
        0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a,
        0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38, 0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a,
    };
    uint8_t got[32];
    uint8_t nine[32];
    uint8_t tag[16];
    uint8_t pt[3];
    uint8_t ct[3];
    uint8_t key[32];
    uint32_t i;
    uint8_t d = 0;
    memset(nine, 0, sizeof(nine));
    nine[0] = 9;
    if (pm_metal_wg_x25519(got, alice_priv, nine) != 0) {
        return -1;
    }
    for (i = 0; i < 32; i++) {
        d = (uint8_t)(d | (got[i] ^ alice_pub[i]));
    }
    if (d != 0) {
        return -1;
    }
    memset(key, 0x42, sizeof(key));
    pt[0] = 'w';
    pt[1] = 'g';
    pt[2] = 0;
    if (pm_metal_wg_aead_encrypt(key, 1, (const uint8_t *)"ad", 2, pt, 2, ct, tag) != 0) {
        return -1;
    }
    memset(pt, 0, sizeof(pt));
    if (pm_metal_wg_aead_decrypt(key, 1, (const uint8_t *)"ad", 2, ct, 2, tag, pt) != 0) {
        return -1;
    }
    if (pt[0] != 'w' || pt[1] != 'g') {
        return -1;
    }
    tag[0] ^= 1u;
    if (pm_metal_wg_aead_decrypt(key, 1, (const uint8_t *)"ad", 2, ct, 2, tag, pt) != -1) {
        return -1;
    }
    /* RFC 7693 A.1 — BLAKE2s-256 of the empty message. */
    {
        static const uint8_t empty_hash[32] = {
            0x69, 0x21, 0x7a, 0x30, 0x79, 0x90, 0x80, 0x94, 0xe1, 0x11, 0x21, 0xd0, 0x42, 0x35, 0x4a, 0x7c,
            0x1f, 0x55, 0xb6, 0x48, 0x2c, 0xa1, 0xa5, 0x1e, 0x1b, 0x25, 0x0d, 0xfd, 0x1e, 0xd0, 0xee, 0xf9,
        };
        uint8_t h[32];
        uint8_t dh = 0;
        pm_metal_wg_blake2s(NULL, 0, h);
        for (i = 0; i < 32; i++) {
            dh = (uint8_t)(dh | (h[i] ^ empty_hash[i]));
        }
        if (dh != 0) {
            return -1;
        }
    }
    return 0;
}
