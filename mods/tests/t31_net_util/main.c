/*
 * T31 — net/{dns,ntp,http} + util/crypto wasi-style imports: Blake2b +
 * AEAD (offline), DNS, SNTP, HTTPS GET. Network steps need a live host
 * (see scripts/verify linux none net).
 */
#include <pymergetic/metal/net/dns.h>
#include <pymergetic/metal/net/http.h>
#include <pymergetic/metal/net/ntp.h>
#include <pymergetic/metal/util/crypto.h>

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
	pm_metal_net_addr_t addrs[4];
	size_t naddr = 0;
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

	/*
	 * Prefer non-Cloudflare HTTPS (qemu user-net SLIRP often gets
	 * ECONNREFUSED to CF). httpbin.org is currently 503-prone; try a
	 * small fallback list so host + qemu verifies stay green.
	 */
	{
		static const char *const hosts[] = { "httpbingo.org", "httpbin.org",
						      "jsonplaceholder.typicode.com" };
		static const char *const urls[] = {
			"https://httpbingo.org/get",
			"https://httpbin.org/get",
			"https://jsonplaceholder.typicode.com/todos/1",
		};
		size_t hi;
		int attempt;
		int ok = 0;

		status = -1;
		for (hi = 0; hi < sizeof(hosts) / sizeof(hosts[0]); hi++) {
			naddr = 0;
			if (pm_metal_net_dns_lookup(hosts[hi], 443, addrs, 4, &naddr) != 0 ||
			    naddr == 0) {
				continue;
			}
			printf("t31_net_util: net dns n=%zu family=%u host=%s\n", naddr,
			       (unsigned)addrs[0].family, hosts[hi]);
			/* HTTPS before NTP — qemu SLIRP UDP retries can starve TCP. */
			for (attempt = 0; attempt < 2; attempt++) {
				body_len = 0;
				status = pm_metal_net_http_get(urls[hi], body, sizeof(body) - 1,
								 &body_len);
				if (status >= 200 && status < 400 && body_len > 0) {
					ok = 1;
					break;
				}
			}
			if (ok) {
				break;
			}
		}
		if (!ok) {
			printf("t31_net_util: net_http_get failed status=%d len=%zu\n", status,
			       body_len);
			return 1;
		}
	}
	body[body_len < sizeof(body) ? body_len : sizeof(body) - 1] = '\0';
	printf("t31_net_util: net http status=%d len=%zu\n", status, body_len);
	if (!strstr(body, "httpbin") && !strstr(body, "httpbingo") && !strstr(body, "\"url\"") &&
	    !strstr(body, "\"userId\"") && !strstr(body, "Example Domain")) {
		printf("t31_net_util: unexpected body\n");
		return 1;
	}
	printf("t31_net_util: net http ok\n");

	if (pm_metal_net_ntp_sync("time.google.com", 5000, &unix_s) != 0 ||
	    unix_s < 1700000000ULL) {
		if (pm_metal_net_ntp_sync("pool.ntp.org", 5000, &unix_s) != 0 ||
		    unix_s < 1700000000ULL) {
			printf("t31_net_util: ntp_sync failed\n");
			return 1;
		}
	}
	printf("t31_net_util: ntp unix=%llu\n", (unsigned long long)unix_s);
	return 0;
}
