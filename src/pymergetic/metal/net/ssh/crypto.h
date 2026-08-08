#ifndef PM_METAL_NET_SSH_CRYPTO_H_
#define PM_METAL_NET_SSH_CRYPTO_H_

#include <stddef.h>
#include <stdint.h>

/* Host key (ssh-ed25519) + X25519/SHA-256 helpers for curve25519-sha256. */
int32_t pm_metal_net_ssh_crypto_init(void);

void pm_metal_net_ssh_crypto_random(uint8_t *out, size_t n);

const uint8_t *pm_metal_net_ssh_host_ed25519_pk(void);
/* 64-byte Monocypher secret key (seed||pk). */
const uint8_t *pm_metal_net_ssh_host_ed25519_sk(void);

void pm_metal_net_ssh_sha256(const uint8_t *data, size_t n, uint8_t out[32]);
void pm_metal_net_ssh_sha256_init(void *ctx);
void pm_metal_net_ssh_sha256_update(void *ctx, const uint8_t *data, size_t n);
void pm_metal_net_ssh_sha256_final(void *ctx, uint8_t out[32]);
size_t pm_metal_net_ssh_sha256_ctx_size(void);

void pm_metal_net_ssh_x25519_keypair(uint8_t sk[32], uint8_t pk[32]);
void pm_metal_net_ssh_x25519(uint8_t shared[32], const uint8_t sk[32],
    const uint8_t peer_pk[32]);

void pm_metal_net_ssh_ed25519_sign(uint8_t sig[64], const uint8_t *msg,
    size_t msg_len);

/* SSH string / mpint helpers into caller buffer; return bytes written or 0. */
uint32_t pm_metal_net_ssh_put_string(uint8_t *dst, uint32_t cap, const void *s,
    uint32_t n);
uint32_t pm_metal_net_ssh_put_mpint(uint8_t *dst, uint32_t cap,
    const uint8_t *be, uint32_t n);
uint32_t pm_metal_net_ssh_put_cstring(uint8_t *dst, uint32_t cap, const char *s);

/* ssh-ed25519 host key blob → dst; returns length or 0. */
uint32_t pm_metal_net_ssh_hostkey_blob(uint8_t *dst, uint32_t cap);

#endif
