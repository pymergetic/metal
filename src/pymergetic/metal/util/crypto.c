/*
 * pm_metal_util_crypto_* — impl: common (see util/crypto.h). Thin
 * pass-through to vendored Monocypher (external/monocypher).
 */
#include "pymergetic/metal/util/crypto.h"

#include "external/monocypher/src/monocypher.h"

int pm_metal_util_crypto_hash(void *hash, size_t hash_len, const void *msg, size_t msg_len)
{
	if (!hash || hash_len != PM_METAL_UTIL_CRYPTO_HASH_LEN || (!msg && msg_len > 0)) {
		return -1;
	}
	crypto_blake2b((uint8_t *)hash, hash_len, (const uint8_t *)msg, msg_len);
	return 0;
}

int pm_metal_util_crypto_aead_lock(void *cipher, size_t cipher_cap, void *mac, size_t mac_cap, const void *key,
				     size_t key_len, const void *nonce, size_t nonce_len, const void *ad,
				     size_t ad_len, const void *plain, size_t plain_len)
{
	if (!cipher || !mac || !key || !nonce || (!plain && plain_len > 0) || (!ad && ad_len > 0)
	    || cipher_cap < plain_len || mac_cap < PM_METAL_UTIL_CRYPTO_MAC_LEN
	    || key_len != PM_METAL_UTIL_CRYPTO_KEY_LEN || nonce_len != PM_METAL_UTIL_CRYPTO_NONCE_LEN) {
		return -1;
	}
	crypto_aead_lock((uint8_t *)cipher, (uint8_t *)mac, (const uint8_t *)key, (const uint8_t *)nonce,
			 (const uint8_t *)ad, ad_len, (const uint8_t *)plain, plain_len);
	return 0;
}

int pm_metal_util_crypto_aead_unlock(void *plain, size_t plain_cap, const void *key, size_t key_len,
				       const void *nonce, size_t nonce_len, const void *mac, size_t mac_len,
				       const void *ad, size_t ad_len, const void *cipher, size_t cipher_len)
{
	if (!plain || !key || !nonce || !mac || (!cipher && cipher_len > 0) || (!ad && ad_len > 0)
	    || plain_cap < cipher_len || mac_len != PM_METAL_UTIL_CRYPTO_MAC_LEN
	    || key_len != PM_METAL_UTIL_CRYPTO_KEY_LEN || nonce_len != PM_METAL_UTIL_CRYPTO_NONCE_LEN) {
		return -1;
	}
	/* Monocypher 4: unlock(plain, mac, key, nonce, ad, cipher). */
	if (crypto_aead_unlock((uint8_t *)plain, (const uint8_t *)mac, (const uint8_t *)key,
			       (const uint8_t *)nonce, (const uint8_t *)ad, ad_len, (const uint8_t *)cipher,
			       cipher_len) != 0) {
		return -1;
	}
	return 0;
}

#include "wasm_export.h"

static int32_t pm_metal_util_crypto_hash_native(wasm_exec_env_t exec_env, void *hash, uint32_t hash_len,
						  const void *msg, uint32_t msg_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_crypto_hash(hash, (size_t)hash_len, msg, (size_t)msg_len);
}

static int32_t pm_metal_util_crypto_aead_lock_native(wasm_exec_env_t exec_env, void *cipher, uint32_t cipher_cap,
						       void *mac, uint32_t mac_cap, const void *key,
						       uint32_t key_len, const void *nonce, uint32_t nonce_len,
						       const void *ad, uint32_t ad_len, const void *plain,
						       uint32_t plain_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_crypto_aead_lock(cipher, (size_t)cipher_cap, mac, (size_t)mac_cap, key,
							(size_t)key_len, nonce, (size_t)nonce_len, ad,
							(size_t)ad_len, plain, (size_t)plain_len);
}

static int32_t pm_metal_util_crypto_aead_unlock_native(wasm_exec_env_t exec_env, void *plain, uint32_t plain_cap,
							 const void *key, uint32_t key_len, const void *nonce,
							 uint32_t nonce_len, const void *mac, uint32_t mac_len,
							 const void *ad, uint32_t ad_len, const void *cipher,
							 uint32_t cipher_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_crypto_aead_unlock(plain, (size_t)plain_cap, key, (size_t)key_len, nonce,
							  (size_t)nonce_len, mac, (size_t)mac_len, ad,
							  (size_t)ad_len, cipher, (size_t)cipher_len);
}

/*
 * Buffer pairs use *~ so WAMR bounds-checks guest linear memory. Fixed-size
 * key/nonce/mac/hash also use *~ (caller passes exact capacity).
 */
static NativeSymbol g_pm_metal_util_crypto_native_symbols[] = {
	{"pm_metal_util_crypto_hash", (void *)pm_metal_util_crypto_hash_native, "(*~*~)i", NULL},
	{"pm_metal_util_crypto_aead_lock", (void *)pm_metal_util_crypto_aead_lock_native, "(*~*~*~*~*~*~)i",
	 NULL},
	{"pm_metal_util_crypto_aead_unlock", (void *)pm_metal_util_crypto_aead_unlock_native, "(*~*~*~*~*~*~)i",
	 NULL},
};

int pm_metal_util_crypto_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_CRYPTO_WASI_MODULE,
					    g_pm_metal_util_crypto_native_symbols,
					    sizeof(g_pm_metal_util_crypto_native_symbols)
						    / sizeof(g_pm_metal_util_crypto_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
