/*
 * T31 — util/crypto wasi-style imports: Blake2b + AEAD (offline).
 */
#include <pymergetic/metal/util/crypto.h>

#include <stdio.h>
#include <string.h>

int
main(void)
{
	static const char msg[] = "pm-metal crypto smoke";
	uint8_t hash[PM_METAL_UTIL_CRYPTO_HASH_LEN];
	uint8_t key[PM_METAL_UTIL_CRYPTO_KEY_LEN];
	uint8_t nonce[PM_METAL_UTIL_CRYPTO_NONCE_LEN];
	uint8_t mac[PM_METAL_UTIL_CRYPTO_MAC_LEN];
	uint8_t cipher[sizeof(msg)];
	uint8_t plain[sizeof(msg)];

	if (pm_metal_util_crypto_hash(hash, sizeof(hash), msg, sizeof(msg)) != 0) {
		printf("t31_crypto: hash failed\n");
		return 1;
	}
	printf("t31_crypto: hash ok first=0x%02x\n", hash[0]);

	memset(key, 0x11, sizeof(key));
	memset(nonce, 0x22, sizeof(nonce));
	if (pm_metal_util_crypto_aead_lock(cipher, sizeof(cipher), mac, sizeof(mac),
					   key, sizeof(key), nonce, sizeof(nonce),
					   NULL, 0, msg, sizeof(msg))
	    != 0) {
		printf("t31_crypto: aead_lock failed\n");
		return 1;
	}
	if (pm_metal_util_crypto_aead_unlock(plain, sizeof(plain), key, sizeof(key),
					     nonce, sizeof(nonce), NULL, 0, cipher,
					     sizeof(cipher), mac, sizeof(mac))
	    != 0) {
		printf("t31_crypto: aead_unlock failed\n");
		return 1;
	}
	if (memcmp(plain, msg, sizeof(msg)) != 0) {
		printf("t31_crypto: aead round-trip mismatch\n");
		return 1;
	}
	printf("t31_crypto: aead round-trip ok\n");
	return 0;
}
