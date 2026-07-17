/*
 * pm_metal_util_ntp_* — impl: common (see util/ntp.h). Minimal SNTP
 * (RFC 4330) client over UDP. Full impl when PM_METAL_HAVE_NET; else stub.
 */
#include "pymergetic/metal/util/ntp.h"

#include <string.h>

#if defined(PM_METAL_HAVE_NET) && PM_METAL_HAVE_NET

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* NTP epoch (1900-01-01) → Unix epoch (1970-01-01) seconds. */
#define PM_METAL_UTIL_NTP_DELTA 2208988800ULL

int pm_metal_util_ntp_sync(const char *server_host, int32_t timeout_ms, uint64_t *out_unix)
{
	const char *host = (server_host && server_host[0]) ? server_host : "pool.ntp.org";
	int timeout = timeout_ms > 0 ? (int)timeout_ms : 3000;
	uint8_t packet[48];
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *ai;
	int fd = -1;
	int rc = -1;
	ssize_t n;
	uint32_t secs;

	if (!out_unix) {
		return -1;
	}

	memset(packet, 0, sizeof(packet));
	/* LI=0, VN=4, Mode=3 (client) */
	packet[0] = 0x23;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	if (getaddrinfo(host, "123", &hints, &res) != 0 || !res) {
		return -1;
	}

	for (ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0) {
			continue;
		}
		struct timeval tv;
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		(void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		(void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		if (sendto(fd, packet, sizeof(packet), 0, ai->ai_addr, ai->ai_addrlen) == (ssize_t)sizeof(packet)) {
			break;
		}
		close(fd);
		fd = -1;
	}

	if (fd < 0) {
		freeaddrinfo(res);
		return -1;
	}

	n = recvfrom(fd, packet, sizeof(packet), 0, NULL, NULL);
	close(fd);
	freeaddrinfo(res);
	if (n < 48) {
		return -1;
	}

	/* Transmit Timestamp seconds — bytes 40..43, network order. */
	memcpy(&secs, packet + 40, sizeof(secs));
	secs = ntohl(secs);
	if (secs < PM_METAL_UTIL_NTP_DELTA) {
		return -1;
	}
	*out_unix = (uint64_t)secs - PM_METAL_UTIL_NTP_DELTA;
	(void)errno;
	rc = 0;
	return rc;
}

#else /* !PM_METAL_HAVE_NET */

int pm_metal_util_ntp_sync(const char *server_host, int32_t timeout_ms, uint64_t *out_unix)
{
	(void)server_host;
	(void)timeout_ms;
	(void)out_unix;
	return -1;
}

#endif /* PM_METAL_HAVE_NET */

#include "wasm_export.h"

static int32_t pm_metal_util_ntp_sync_native(wasm_exec_env_t exec_env, const char *server_host,
					       int32_t timeout_ms, uint64_t *out_unix)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_ntp_sync(server_host, timeout_ms, out_unix);
}

static NativeSymbol g_pm_metal_util_ntp_native_symbols[] = {
	{"pm_metal_util_ntp_sync", (void *)pm_metal_util_ntp_sync_native, "($i*)i", NULL},
};

int pm_metal_util_ntp_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_NTP_WASI_MODULE, g_pm_metal_util_ntp_native_symbols,
					    sizeof(g_pm_metal_util_ntp_native_symbols)
						    / sizeof(g_pm_metal_util_ntp_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
