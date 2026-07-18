/*
 * Host libc networking for native_sim Metal port/{dns,udp} + HTTPS.
 * Built in the native_simulator host context (real sys/socket.h).
 */
#include "pymergetic/metal/net/addr.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static void pm_metal_host_addr_from_sockaddr(pm_metal_net_addr_t *out, const struct sockaddr *sa)
{
	memset(out, 0, sizeof(*out));
	if (sa->sa_family == AF_INET) {
		const struct sockaddr_in *in = (const struct sockaddr_in *)sa;

		out->family = PM_METAL_NET_AF_INET;
		out->port = ntohs(in->sin_port);
		memcpy(out->ip, &in->sin_addr.s_addr, 4);
	} else if (sa->sa_family == AF_INET6) {
		const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;

		out->family = PM_METAL_NET_AF_INET6;
		out->port = ntohs(in6->sin6_port);
		memcpy(out->ip, &in6->sin6_addr.s6_addr, 16);
	}
}

static int pm_metal_host_addr_to_sockaddr(const pm_metal_net_addr_t *a, struct sockaddr_storage *ss,
					   socklen_t *len)
{
	memset(ss, 0, sizeof(*ss));
	if (a->family == PM_METAL_NET_AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *)ss;

		in->sin_family = AF_INET;
		in->sin_port = htons(a->port);
		memcpy(&in->sin_addr.s_addr, a->ip, 4);
		*len = sizeof(*in);
		return 0;
	}
	if (a->family == PM_METAL_NET_AF_INET6) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ss;

		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons(a->port);
		memcpy(&in6->sin6_addr.s6_addr, a->ip, 16);
		*len = sizeof(*in6);
		return 0;
	}
	return -1;
}

int pm_metal_host_dns_lookup(const char *host, uint16_t port, pm_metal_net_addr_t *out, size_t out_cap,
			       size_t *out_n)
{
	char portstr[8];
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *ai;
	size_t n = 0;

	if (!host || !host[0] || !out || out_cap == 0 || !out_n) {
		return -1;
	}
	(void)snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
		return -1;
	}
	for (ai = res; ai && n < out_cap; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
			continue;
		}
		pm_metal_host_addr_from_sockaddr(&out[n], ai->ai_addr);
		if (out[n].family == 0) {
			continue;
		}
		n++;
	}
	freeaddrinfo(res);
	if (n == 0) {
		return -1;
	}
	*out_n = n;
	return 0;
}

int pm_metal_host_udp_open(uint8_t family)
{
	int af = (family == PM_METAL_NET_AF_INET)    ? AF_INET
		 : (family == PM_METAL_NET_AF_INET6) ? AF_INET6
						      : -1;

	if (af < 0) {
		return -1;
	}
	return socket(af, SOCK_DGRAM, IPPROTO_UDP);
}

int pm_metal_host_udp_close(int fd)
{
	return (fd < 0) ? -1 : (close(fd) == 0 ? 0 : -1);
}

int pm_metal_host_udp_set_timeout_ms(int fd, int32_t timeout_ms)
{
	struct timeval tv;
	int timeout = timeout_ms > 0 ? (int)timeout_ms : 3000;

	if (fd < 0) {
		return -1;
	}
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
		return -1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
		return -1;
	}
	return 0;
}

int pm_metal_host_udp_sendto(int fd, const void *buf, uint32_t len, const pm_metal_net_addr_t *to)
{
	struct sockaddr_storage ss;
	socklen_t slen;

	if (fd < 0 || !buf || len == 0 || !to) {
		return -1;
	}
	if (pm_metal_host_addr_to_sockaddr(to, &ss, &slen) != 0) {
		return -1;
	}
	if (sendto(fd, buf, len, 0, (struct sockaddr *)&ss, slen) != (ssize_t)len) {
		return -1;
	}
	return 0;
}

int pm_metal_host_udp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
{
	ssize_t n;

	if (fd < 0 || !buf || cap == 0 || !out_len) {
		return -1;
	}
	n = recvfrom(fd, buf, cap, 0, NULL, NULL);
	if (n <= 0) {
		return -1;
	}
	*out_len = (uint32_t)n;
	return 0;
}

typedef struct pm_metal_host_http_buf {
	uint8_t *dst;
	size_t cap;
	size_t len;
} pm_metal_host_http_buf_t;

static size_t pm_metal_host_http_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	pm_metal_host_http_buf_t *b = (pm_metal_host_http_buf_t *)userdata;
	size_t n = size * nmemb;
	size_t room = (b->len < b->cap) ? (b->cap - b->len) : 0;
	size_t take = (n < room) ? n : room;

	if (take > 0) {
		memcpy(b->dst + b->len, ptr, take);
		b->len += take;
	}
	return n;
}

static int pm_metal_host_http_ready;

int pm_metal_host_http_init(void)
{
	if (pm_metal_host_http_ready) {
		return 0;
	}
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		return -1;
	}
	pm_metal_host_http_ready = 1;
	return 0;
}

int pm_metal_host_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len,
			     const char *ca_path)
{
	CURL *curl;
	CURLcode cres;
	pm_metal_host_http_buf_t buf;
	long status = 0;

	if (!url || !url[0] || !dst || dst_cap == 0 || !pm_metal_host_http_ready || !ca_path) {
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
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pm_metal_host_http_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "pm-metal-net-http/1.0");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);
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
	return (status >= 100 && status <= 599) ? (int)status : -1;
}
