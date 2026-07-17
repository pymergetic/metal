/*
 * pm_metal_util_http_* — impl: common (see util/http.h). Thin curl wrapper
 * when PM_METAL_HAVE_NET; else stub.
 */
#include "pymergetic/metal/util/http.h"

#include <string.h>

#if defined(PM_METAL_HAVE_NET) && PM_METAL_HAVE_NET

#include <curl/curl.h>
#include <stdlib.h>

typedef struct pm_metal_util_http_buf {
	uint8_t *dst;
	size_t cap;
	size_t len;
} pm_metal_util_http_buf_t;

static size_t pm_metal_util_http_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	pm_metal_util_http_buf_t *b = (pm_metal_util_http_buf_t *)userdata;
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
	/* Always claim full n so curl does not abort on truncation. */
	return n;
}

static int pm_metal_util_http_global_ok;

int pm_metal_util_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len)
{
	CURL *curl;
	CURLcode cres;
	pm_metal_util_http_buf_t buf;
	long status = 0;

	if (!url || !url[0] || !dst || dst_cap == 0) {
		return -1;
	}
	if (!pm_metal_util_http_global_ok) {
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
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pm_metal_util_http_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "pm-metal-util-http/1.0");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	/* Prefer HTTP/2 when the peer offers it (nghttp2 linked in). */
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
	/* System CA store on linux; mbedtls backend still honors this. */
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

	cres = curl_easy_perform(curl);
	if (cres != CURLE_OK) {
		curl_easy_cleanup(curl);
		return -1;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);

	if (out_len) {
		*out_len = buf.len;
	}
	if (status < 100 || status > 599) {
		return -1;
	}
	return (int)status;
}

#else /* !PM_METAL_HAVE_NET */

int pm_metal_util_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len)
{
	(void)url;
	(void)dst;
	(void)dst_cap;
	(void)out_len;
	return -1;
}

#endif /* PM_METAL_HAVE_NET */

#include "wasm_export.h"

static int32_t pm_metal_util_http_get_native(wasm_exec_env_t exec_env, const char *url, void *dst,
					       uint32_t dst_cap, uint32_t *out_len)
{
	size_t n = 0;
	int rc;

	(void)exec_env;
	rc = pm_metal_util_http_get(url, dst, (size_t)dst_cap, &n);
	if (out_len) {
		*out_len = (uint32_t)n;
	}
	return (int32_t)rc;
}

static NativeSymbol g_pm_metal_util_http_native_symbols[] = {
	{"pm_metal_util_http_get", (void *)pm_metal_util_http_get_native, "($*~*)i", NULL},
};

int pm_metal_util_http_native_register(void)
{
#if defined(PM_METAL_HAVE_NET) && PM_METAL_HAVE_NET
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		return -1;
	}
	pm_metal_util_http_global_ok = 1;
#endif
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_HTTP_WASI_MODULE, g_pm_metal_util_http_native_symbols,
					    sizeof(g_pm_metal_util_http_native_symbols)
						    / sizeof(g_pm_metal_util_http_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
