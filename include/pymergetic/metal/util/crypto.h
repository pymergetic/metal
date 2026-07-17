/*
 * Authenticated encryption + hashing — thin wrapper around vendored
 * Monocypher (external/monocypher, pinned by scripts/setup-monocypher.sh).
 * Host-side only (src/common/pymergetic/metal/util/crypto.c); wasm32 mods
 * call through this module's wasi-style import bridge, never link
 * Monocypher themselves (same shape as util/lz4.h).
 *
 * Deliberately a small leaf API: Blake2b and ChaCha20-Poly1305 AEAD.
 * TLS/HTTPS lives in util/http.h (libcurl + mbedTLS), not here.
 */
#ifndef PYMERGETIC_METAL_UTIL_CRYPTO_H_
#define PYMERGETIC_METAL_UTIL_CRYPTO_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/util/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_UTIL_CRYPTO_WASI_MODULE "pymergetic.metal.util.crypto"

/* Blake2b digest length used by hash() below (Monocypher's default). */
#define PM_METAL_UTIL_CRYPTO_HASH_LEN 64
/* ChaCha20-Poly1305 key / nonce / mac sizes (Monocypher AEAD). */
#define PM_METAL_UTIL_CRYPTO_KEY_LEN 32
#define PM_METAL_UTIL_CRYPTO_NONCE_LEN 24
#define PM_METAL_UTIL_CRYPTO_MAC_LEN 16

#if defined(__wasm__)
#define PM_METAL_UTIL_CRYPTO_IMPORT(name) \
	PM_METAL_UTIL_WASI_IMPORT(PM_METAL_UTIL_CRYPTO_WASI_MODULE, name)
#endif

/*
 * Blake2b(msg) → hash (must be HASH_LEN bytes). Returns 0 on success, -1
 * if any pointer is NULL or hash_len != HASH_LEN.
 *
 * impl: common — src/common/pymergetic/metal/util/crypto.c
 * impl: wasi import — src/common/pymergetic/metal/util/crypto.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_crypto_hash(void *hash, size_t hash_len, const void *msg, size_t msg_len)
	PM_METAL_UTIL_CRYPTO_IMPORT(pm_metal_util_crypto_hash);
#else
int pm_metal_util_crypto_hash(void *hash, size_t hash_len, const void *msg, size_t msg_len);
#endif

/*
 * AEAD seal: writes plain_len ciphertext bytes into cipher (cipher_cap
 * must be >= plain_len) and MAC_LEN bytes into mac. key_len/nonce_len/
 * mac_cap must be KEY_LEN / NONCE_LEN / MAC_LEN. ad may be NULL when
 * ad_len is 0. Returns 0 on success, -1 on bad args.
 *
 * Buffer lengths are part of the contract (wasi *~ pairs need them).
 *
 * impl: common — src/common/pymergetic/metal/util/crypto.c
 * impl: wasi import — src/common/pymergetic/metal/util/crypto.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_crypto_aead_lock(void *cipher, size_t cipher_cap, void *mac, size_t mac_cap,
					   const void *key, size_t key_len, const void *nonce, size_t nonce_len,
					   const void *ad, size_t ad_len, const void *plain, size_t plain_len)
	PM_METAL_UTIL_CRYPTO_IMPORT(pm_metal_util_crypto_aead_lock);
#else
int pm_metal_util_crypto_aead_lock(void *cipher, size_t cipher_cap, void *mac, size_t mac_cap, const void *key,
				     size_t key_len, const void *nonce, size_t nonce_len, const void *ad,
				     size_t ad_len, const void *plain, size_t plain_len);
#endif

/*
 * AEAD open: inverse of aead_lock(). Returns 0 on success, -1 on bad args
 * or authentication failure (plain is not written on MAC mismatch).
 *
 * impl: common — src/common/pymergetic/metal/util/crypto.c
 * impl: wasi import — src/common/pymergetic/metal/util/crypto.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_crypto_aead_unlock(void *plain, size_t plain_cap, const void *key, size_t key_len,
					     const void *nonce, size_t nonce_len, const void *mac, size_t mac_len,
					     const void *ad, size_t ad_len, const void *cipher, size_t cipher_len)
	PM_METAL_UTIL_CRYPTO_IMPORT(pm_metal_util_crypto_aead_unlock);
#else
int pm_metal_util_crypto_aead_unlock(void *plain, size_t plain_cap, const void *key, size_t key_len,
				       const void *nonce, size_t nonce_len, const void *mac, size_t mac_len,
				       const void *ad, size_t ad_len, const void *cipher, size_t cipher_len);
#endif

#if !defined(__wasm__)
/*
 * Registers this module's wasi-style imports. Call once from runtime init.
 *
 * impl: common — src/common/pymergetic/metal/util/crypto.c
 */
int pm_metal_util_crypto_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_CRYPTO_H_ */
