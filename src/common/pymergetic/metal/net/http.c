/*
 * Net HTTP transport — libcurl when PM_METAL_HAVE_CURL; mbedTLS+net/tcp
 * when PM_METAL_HAVE_MBEDTLS; otherwise stub. CA from net/tls (→ port/tls)
 * with an embedded root fallback for the mbedtls path.
 */
#include "pymergetic/metal/net/http.h"

#include "pymergetic/metal/net/tls.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Unix time from the last successful GET's Date header (0 if none). */
static uint64_t g_pm_metal_net_http_last_date_unix;

uint64_t pm_metal_net_http_last_date_unix(void)
{
	return g_pm_metal_net_http_last_date_unix;
}

#if defined(PM_METAL_HAVE_CURL) && PM_METAL_HAVE_CURL

#if defined(__ZEPHYR__)
/* native_sim: curl lives in host_net_adapt.c (host libC). */
int pm_metal_host_http_init(void);
int pm_metal_host_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len,
			     const char *ca_path);

int pm_metal_net_http_init(void)
{
	return pm_metal_host_http_init();
}

int pm_metal_net_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len)
{
	char ca_path[512];

	if (pm_metal_net_tls_ca_file(ca_path, sizeof(ca_path)) != 0) {
		return -1;
	}
	return pm_metal_host_http_get(url, dst, dst_cap, out_len, ca_path);
}
#else /* Linux / NuttX host curl */

#include <curl/curl.h>

typedef struct pm_metal_net_http_buf {
	uint8_t *dst;
	size_t cap;
	size_t len;
} pm_metal_net_http_buf_t;

static size_t pm_metal_net_http_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	pm_metal_net_http_buf_t *b = (pm_metal_net_http_buf_t *)userdata;
	size_t n = size * nmemb;
	size_t room;
	size_t take;

	if (!b || !b->dst) {
		return 0;
	}
	room = (b->len < b->cap) ? (b->cap - b->len) : 0;
	take = (n < room) ? n : room;
	if (take > 0) {
		memcpy(b->dst + b->len, ptr, take);
		b->len += take;
	}
	return n;
}

static int pm_metal_net_http_ready;

int pm_metal_net_http_init(void)
{
	if (pm_metal_net_http_ready) {
		return 0;
	}
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		return -1;
	}
	pm_metal_net_http_ready = 1;
	return 0;
}

int pm_metal_net_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len)
{
	CURL *curl;
	CURLcode cres;
	pm_metal_net_http_buf_t buf;
	long status = 0;
	char ca_path[512];

	if (!url || !url[0] || !dst || dst_cap == 0 || !pm_metal_net_http_ready) {
		return -1;
	}
	g_pm_metal_net_http_last_date_unix = 0;
	if (pm_metal_net_tls_ca_file(ca_path, sizeof(ca_path)) != 0) {
		return -1;
	}

	buf.dst = (uint8_t *)dst;
	buf.cap = dst_cap;
	buf.len = 0;

	curl = curl_easy_init();
	if (!curl) {
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pm_metal_net_http_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "pm-metal-net-http/1.0");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);

	cres = curl_easy_perform(curl);
	if (cres != CURLE_OK) {
		curl_easy_cleanup(curl);
		return -1;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	{
		long filetime = -1;

		if (curl_easy_getinfo(curl, CURLINFO_FILETIME, &filetime) == CURLE_OK &&
		    filetime > 0) {
			g_pm_metal_net_http_last_date_unix = (uint64_t)filetime;
		}
	}
	curl_easy_cleanup(curl);

	if (out_len) {
		*out_len = buf.len;
	}
	if (status < 100 || status > 599) {
		return -1;
	}
	return (int)status;
}
#endif /* !__ZEPHYR__ */

#elif defined(PM_METAL_HAVE_MBEDTLS) && PM_METAL_HAVE_MBEDTLS

#include "pymergetic/metal/net/dns.h"
#include "pymergetic/metal/net/tcp.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <stdlib.h>

#include "http_ca_pem.inc"

/* curl path uses CURLOPT_FILETIME; mbedtls parses the Date header instead. */
static int pm_metal_net_http_parse_date_hdr(const char *hdrs, uint64_t *out_unix)
{
	static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
					"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static const uint16_t mdays[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	const char *p;
	char mon[4];
	int day = 0;
	int year = 0;
	int hour = 0;
	int min = 0;
	int sec = 0;
	int mi;
	uint64_t days;

	if (!hdrs || !out_unix) {
		return -1;
	}
	p = hdrs;
	for (;;) {
		const char *line = p;
		const char *nl = strstr(p, "\r\n");
		size_t len = nl ? (size_t)(nl - p) : strlen(p);

		if (len >= 6 && (line[0] == 'D' || line[0] == 'd') &&
		    (line[1] == 'a' || line[1] == 'A') && (line[2] == 't' || line[2] == 'T') &&
		    (line[3] == 'e' || line[3] == 'E') && line[4] == ':') {
			const char *v = line + 5;

			while (*v == ' ' || *v == '\t') {
				v++;
			}
			/* RFC 7231 IMF-fix: Sun, 06 Nov 1994 08:49:37 GMT */
			if (sscanf(v, "%*3s, %d %3s %d %d:%d:%d", &day, mon, &year, &hour, &min, &sec) !=
			    6) {
				return -1;
			}
			for (mi = 0; mi < 12; mi++) {
				if (memcmp(mon, months[mi], 3) == 0) {
					break;
				}
			}
			if (mi >= 12 || day < 1 || day > 31 || year < 1970 || hour > 23 || min > 59 ||
			    sec > 60) {
				return -1;
			}
			days = (uint64_t)(year - 1970) * 365ull + (uint64_t)((year - 1969) / 4);
			days += mdays[mi] + (uint64_t)day - 1ull;
			if (mi > 1 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
				days += 1ull;
			}
			*out_unix = days * 86400ull + (uint64_t)hour * 3600ull + (uint64_t)min * 60ull +
				    (uint64_t)sec;
			return 0;
		}
		if (!nl) {
			break;
		}
		p = nl + 2;
		if (p[0] == '\0' || (p[0] == '\r' && p[1] == '\n')) {
			break;
		}
	}
	return -1;
}

typedef struct pm_metal_net_http_bio {
	int fd;
} pm_metal_net_http_bio_t;

static int pm_metal_net_http_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
	pm_metal_net_http_bio_t *bio = (pm_metal_net_http_bio_t *)ctx;
	uint32_t sent = 0;

	if (!bio || bio->fd < 0 || !buf) {
		return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
	}
	if (pm_metal_net_tcp_send(bio->fd, buf, (uint32_t)len, &sent) != 0 || sent == 0) {
		return MBEDTLS_ERR_SSL_CONN_EOF;
	}
	return (int)sent;
}

static int pm_metal_net_http_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
	pm_metal_net_http_bio_t *bio = (pm_metal_net_http_bio_t *)ctx;
	uint32_t got = 0;

	if (!bio || bio->fd < 0 || !buf) {
		return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
	}
	if (pm_metal_net_tcp_recv(bio->fd, buf, (uint32_t)len, &got) != 0) {
		return MBEDTLS_ERR_SSL_CONN_EOF;
	}
	if (got == 0) {
		return MBEDTLS_ERR_SSL_WANT_READ;
	}
	return (int)got;
}

static int pm_metal_net_http_parse_url(const char *url, char *host, size_t host_cap, uint16_t *port,
					const char **path_out)
{
	const char *p;
	const char *slash;
	const char *colon;
	size_t host_len;

	if (!url || strncmp(url, "https://", 8) != 0) {
		return -1;
	}
	p = url + 8;
	slash = strchr(p, '/');
	host_len = slash ? (size_t)(slash - p) : strlen(p);
	if (host_len == 0 || host_len + 1 > host_cap) {
		return -1;
	}
	*port = 443;
	colon = memchr(p, ':', host_len);
	if (colon) {
		unsigned long pr;
		char tmp[8];
		size_t plen = host_len - (size_t)(colon - p) - 1;

		host_len = (size_t)(colon - p);
		if (host_len == 0 || plen == 0 || plen >= sizeof(tmp)) {
			return -1;
		}
		memcpy(tmp, colon + 1, plen);
		tmp[plen] = '\0';
		pr = strtoul(tmp, NULL, 10);
		if (pr == 0 || pr > 65535UL) {
			return -1;
		}
		*port = (uint16_t)pr;
	}
	memcpy(host, p, host_len);
	host[host_len] = '\0';
	*path_out = slash ? slash : "/";
	return 0;
}

static int pm_metal_net_http_load_cas(mbedtls_x509_crt *ca)
{
	int rc;

#if defined(MBEDTLS_FS_IO)
	{
		char ca_path[512];

		if (pm_metal_net_tls_ca_file(ca_path, sizeof(ca_path)) == 0) {
			rc = mbedtls_x509_crt_parse_file(ca, ca_path);
			if (rc == 0) {
				return 0;
			}
		}
	}
#endif
	rc = mbedtls_x509_crt_parse(ca, (const unsigned char *)g_pm_metal_ca_digicert_g2,
				     strlen(g_pm_metal_ca_digicert_g2) + 1);
	if (rc != 0) {
		return -1;
	}
	rc = mbedtls_x509_crt_parse(ca, (const unsigned char *)g_pm_metal_ca_isrg_x1,
				     strlen(g_pm_metal_ca_isrg_x1) + 1);
	if (rc != 0) {
		return -1;
	}
	rc = mbedtls_x509_crt_parse(ca, (const unsigned char *)g_pm_metal_ca_sslcom_ecc_2022,
				     strlen(g_pm_metal_ca_sslcom_ecc_2022) + 1);
	if (rc != 0) {
		return -1;
	}
	rc = mbedtls_x509_crt_parse(ca, (const unsigned char *)g_pm_metal_ca_amazon_root_ca1,
				     strlen(g_pm_metal_ca_amazon_root_ca1) + 1);
	return rc == 0 ? 0 : -1;
}

#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
void pm_metal_mbedtls_heap_init(void);
#endif

int pm_metal_net_http_init(void)
{
#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
	pm_metal_mbedtls_heap_init();
#endif
	return 0;
}

int pm_metal_net_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len)
{
	char host[256];
	uint16_t port = 443;
	const char *path = "/";
	pm_metal_net_addr_t addrs[4];
	size_t naddr = 0;
	size_t i;
	int fd = -1;
	int status = -1;
	char req[512];
	int req_len;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt ca;
	pm_metal_net_http_bio_t bio;
	unsigned char acc[2048];
	size_t acc_len = 0;
	int hdr_done = 0;
	size_t body_len = 0;
	uint8_t *body = (uint8_t *)dst;

	if (out_len) {
		*out_len = 0;
	}
	g_pm_metal_net_http_last_date_unix = 0;
	if (!url || !url[0] || !dst || dst_cap == 0) {
		return -1;
	}
#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
	pm_metal_mbedtls_heap_init();
#endif
	if (pm_metal_net_http_parse_url(url, host, sizeof(host), &port, &path) != 0) {
		return -1;
	}
	if (pm_metal_net_dns_lookup(host, port, addrs, 4, &naddr) != 0 || naddr == 0) {
		return -1;
	}

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_x509_crt_init(&ca);

	{
		int mbed_rc;

		mbed_rc = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
		if (mbed_rc != 0) {
			goto done;
		}
		if (pm_metal_net_http_load_cas(&ca) != 0) {
			goto done;
		}
		mbed_rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
						       MBEDTLS_SSL_TRANSPORT_STREAM,
						       MBEDTLS_SSL_PRESET_DEFAULT);
		if (mbed_rc != 0) {
			goto done;
		}
		mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
		mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
		mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

		for (i = 0; i < naddr; i++) {
			addrs[i].port = port;
			fd = pm_metal_net_tcp_open(addrs[i].family);
			if (fd < 0) {
				continue;
			}
			/* Connect first — some stacks mishandle SO_*TIMEO before SYN. */
			if (pm_metal_net_tcp_connect(fd, &addrs[i]) == 0) {
				(void)pm_metal_net_tcp_set_timeout_ms(fd, 30000);
				break;
			}
			(void)pm_metal_net_tcp_close(fd);
			fd = -1;
		}
		if (fd < 0) {
			goto done;
		}

		mbed_rc = mbedtls_ssl_setup(&ssl, &conf);
		if (mbed_rc != 0) {
			goto done;
		}
		mbed_rc = mbedtls_ssl_set_hostname(&ssl, host);
		if (mbed_rc != 0) {
			goto done;
		}

		bio.fd = fd;
		mbedtls_ssl_set_bio(&ssl, &bio, pm_metal_net_http_bio_send, pm_metal_net_http_bio_recv,
				     NULL);
		mbed_rc = mbedtls_ssl_handshake(&ssl);
		if (mbed_rc != 0) {
			goto done;
		}
	}

	req_len = snprintf(req, sizeof(req),
			     "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: pm-metal-net-http/1.0\r\n"
			     "Accept: */*\r\nConnection: close\r\n\r\n",
			     path, host);
	if (req_len <= 0 || (size_t)req_len >= sizeof(req)) {
		goto done;
	}
	{
		size_t off = 0;

		while (off < (size_t)req_len) {
			int n = mbedtls_ssl_write(&ssl, (const unsigned char *)req + off,
						    (size_t)req_len - off);

			if (n <= 0) {
				goto done;
			}
			off += (size_t)n;
		}
	}

	for (;;) {
		unsigned char chunk[512];
		int n = mbedtls_ssl_read(&ssl, chunk, sizeof(chunk));

		if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
			continue;
		}
		if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) {
			break;
		}
		if (n < 0) {
			goto done;
		}
		if (!hdr_done) {
			size_t take = (size_t)n;
			size_t room = sizeof(acc) - acc_len - 1;
			char *sep;

			if (take > room) {
				take = room;
			}
			memcpy(acc + acc_len, chunk, take);
			acc_len += take;
			acc[acc_len] = '\0';
			sep = strstr((char *)acc, "\r\n\r\n");
			if (!sep) {
				if (acc_len + 1 >= sizeof(acc)) {
					goto done;
				}
				continue;
			}
			{
				size_t hsz = (size_t)(sep - (char *)acc) + 4;
				size_t rem = acc_len - hsz;
				int maj = 0;
				int min = 0;
				int st = 0;

				if (sscanf((char *)acc, "HTTP/%d.%d %d", &maj, &min, &st) != 3) {
					goto done;
				}
				status = st;
				hdr_done = 1;
				{
					uint64_t date_unix = 0;

					if (pm_metal_net_http_parse_date_hdr((char *)acc, &date_unix) ==
					    0) {
						g_pm_metal_net_http_last_date_unix = date_unix;
					}
				}
				if (rem > 0) {
					size_t copy = rem < dst_cap ? rem : dst_cap;

					memcpy(body, acc + hsz, copy);
					body_len = copy;
				}
			}
			continue;
		}
		if (body_len < dst_cap) {
			size_t copy = (size_t)n;

			if (body_len + copy > dst_cap) {
				copy = dst_cap - body_len;
			}
			memcpy(body + body_len, chunk, copy);
			body_len += copy;
		}
	}

	if (out_len) {
		*out_len = body_len;
	}
	if (status < 100 || status > 599) {
		status = -1;
	}

done:
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&conf);
	mbedtls_x509_crt_free(&ca);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
	if (fd >= 0) {
		(void)pm_metal_net_tcp_close(fd);
	}
	return status;
}

#else /* !CURL && !MBEDTLS */

int pm_metal_net_http_init(void)
{
	return 0;
}

int pm_metal_net_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len)
{
	(void)url;
	(void)dst;
	(void)dst_cap;
	(void)out_len;
	return -1;
}

#endif

#include "wasm_export.h"

#include <stddef.h>

static int32_t pm_metal_net_http_get_native(wasm_exec_env_t exec_env, const char *url, void *dst,
					      uint32_t dst_cap, uint32_t *out_len)
{
	size_t n = 0;
	int rc;

	(void)exec_env;
	rc = pm_metal_net_http_get(url, dst, (size_t)dst_cap, &n);
	if (out_len) {
		*out_len = (uint32_t)n;
	}
	return (int32_t)rc;
}

static NativeSymbol g_pm_metal_net_http_native_symbols[] = {
	{"pm_metal_net_http_get", (void *)pm_metal_net_http_get_native, "($*~*)i", NULL},
};

int pm_metal_net_http_native_register(void)
{
	if (pm_metal_net_http_init() != 0) {
		return -1;
	}
	if (!wasm_runtime_register_natives(PM_METAL_NET_HTTP_WASI_MODULE, g_pm_metal_net_http_native_symbols,
					    sizeof(g_pm_metal_net_http_native_symbols)
						    / sizeof(g_pm_metal_net_http_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
