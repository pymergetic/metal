/*
 * T31 — util/{crypto,ntp,http}.h wasi-style imports: Blake2b + AEAD
 * round-trip (offline), SNTP sync, HTTPS GET. Network steps need a live
 * host (see scripts/verify-linux-net.sh).
 */
#include <pymergetic/metal/util/crypto.h>
#include <pymergetic/metal/util/http.h>
#include <pymergetic/metal/util/ntp.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
	static const char msg[] = "pm-metal crypto smoke";
	uint8_t hash[PM_METAL_UTIL_CRYPTO_HASH_LEN];
	uint8_t key[PM_METAL_UTIL_CRYPTO_KEY_LEN];
	uint8_t nonce[PM_METAL_UTIL_CRYPTO_NONCE_LEN];
	uint8_t mac[PM_METAL_UTIL_CRYPTO_MAC_LEN];
	uint8_t cipher[sizeof(msg)];
	uint8_t plain[sizeof(msg)];
	uint64_t unix_s = 0;
	static char body[4096];
	size_t body_len = 0;
	int status;

	if (pm_metal_util_crypto_hash(hash, sizeof(hash), msg, sizeof(msg)) != 0) {
		printf("t31_net_util: hash failed\n");
		return 1;
	}
	printf("t31_net_util: hash ok first=0x%02x\n", hash[0]);

	memset(key, 0x11, sizeof(key));
	memset(nonce, 0x22, sizeof(nonce));
	if (pm_metal_util_crypto_aead_lock(cipher, sizeof(cipher), mac, sizeof(mac), key, sizeof(key),
					     nonce, sizeof(nonce), NULL, 0, msg, sizeof(msg))
	    != 0) {
		printf("t31_net_util: aead_lock failed\n");
		return 1;
	}
	if (pm_metal_util_crypto_aead_unlock(plain, sizeof(plain), key, sizeof(key), nonce, sizeof(nonce),
					       mac, sizeof(mac), NULL, 0, cipher, sizeof(cipher))
	    != 0) {
		printf("t31_net_util: aead_unlock failed\n");
		return 1;
	}
	if (memcmp(plain, msg, sizeof(msg)) != 0) {
		printf("t31_net_util: aead round-trip mismatch\n");
		return 1;
	}
	printf("t31_net_util: aead round-trip ok\n");

	if (pm_metal_util_ntp_sync("pool.ntp.org", 5000, &unix_s) != 0 || unix_s < 1700000000ULL) {
		printf("t31_net_util: ntp_sync failed\n");
		return 1;
	}
	printf("t31_net_util: ntp unix=%llu\n", (unsigned long long)unix_s);

	status = pm_metal_util_http_get("https://example.com/", body, sizeof(body) - 1, &body_len);
	if (status < 200 || status >= 400 || body_len == 0) {
		printf("t31_net_util: http_get failed status=%d len=%zu\n", status, body_len);
		return 1;
	}
	body[body_len < sizeof(body) ? body_len : sizeof(body) - 1] = '\0';
	printf("t31_net_util: http status=%d len=%zu\n", status, body_len);
	if (!strstr(body, "Example Domain") && !strstr(body, "example")) {
		printf("t31_net_util: unexpected body\n");
		return 1;
	}
	printf("t31_net_util: http ok\n");
	return 0;
}
