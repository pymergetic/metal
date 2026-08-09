/*
 * Code trust — EdDSA-BLAKE2b (Monocypher) verify + off/soft/enforce policy.
 * No second crypto lib; PKI bake / HTTP fetch are later waves.
 */
#ifndef PYMERGETIC_METAL_TRUST_H_
#define PYMERGETIC_METAL_TRUST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_TRUST_MODE_OFF     0
#define PM_METAL_TRUST_MODE_SOFT    1
#define PM_METAL_TRUST_MODE_ENFORCE 2

#define PM_METAL_TRUST_PUBKEY_LEN 32u
#define PM_METAL_TRUST_SIG_LEN    64u

/** Compile-time policy from Kconfig (autoconf). */
int32_t pm_metal_trust_mode(void);

/** "off" / "soft" / "enforce". */
const char *pm_metal_trust_mode_str(void);

/** 1 if a mods public key is installed. */
int32_t pm_metal_trust_ready(void);

/**
 * Install the Mods EdDSA public key (32 bytes). Replaces any previous key.
 * pk may be NULL / pk_len 0 to clear.
 */
int32_t pm_metal_trust_mods_pubkey_set(const uint8_t *pk, uint32_t pk_len);

/**
 * Pure crypto: verify detached EdDSA sig with the installed Mods key.
 * Returns 0 on success.
 */
int32_t pm_metal_trust_verify_mods(const void *data,
                                   uint32_t data_len,
                                   const void *sig,
                                   uint32_t sig_len);

/**
 * Policy gate: off -> ok; soft -> ok if no sig else verify;
 * enforce -> sig required + verify. sig may be NULL / sig_len 0.
 */
int32_t pm_metal_trust_accept_mods(const void *data,
                                   uint32_t data_len,
                                   const void *sig,
                                   uint32_t sig_len);

/**
 * Host/boot proof: known EdDSA vector + soft accept of unsigned body.
 * Logs `trust verify ok`. Returns 0 on success.
 */
int32_t pm_metal_trust_proof(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_TRUST_H_ */
