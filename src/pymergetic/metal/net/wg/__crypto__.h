/* pymergetic.metal.net.wg — X25519 + ChaCha20-Poly1305 + BLAKE2s HKDF (WireGuard HASH). */
#ifndef PYMERGETIC_METAL_NET_WG_CRYPTO_H
#define PYMERGETIC_METAL_NET_WG_CRYPTO_H

#include <stdint.h>

int32_t pm_metal_wg_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]);
void pm_metal_wg_x25519_base(uint8_t out[32], const uint8_t scalar[32]);
int32_t pm_metal_wg_aead_encrypt(const uint8_t key[32], uint64_t nonce, const uint8_t *ad,
    uint32_t ad_len, const uint8_t *pt, uint32_t pt_len, uint8_t *ct, uint8_t tag[16]);
int32_t pm_metal_wg_aead_decrypt(const uint8_t key[32], uint64_t nonce, const uint8_t *ad,
    uint32_t ad_len, const uint8_t *ct, uint32_t ct_len, const uint8_t tag[16], uint8_t *pt);
void pm_metal_wg_hkdf2(const uint8_t ck[32], const uint8_t *ikm, uint32_t ikm_len, uint8_t out1[32],
    uint8_t out2[32]);
void pm_metal_wg_blake2s(const uint8_t *in, uint32_t n, uint8_t out[32]);
int32_t pm_metal_wg_crypto_selftest(void);

#endif /* PYMERGETIC_METAL_NET_WG_CRYPTO_H */
