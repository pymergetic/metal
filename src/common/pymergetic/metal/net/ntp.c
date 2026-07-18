/*
 * pm_metal_net_ntp_* — SNTP (RFC 4330) over net/{dns,udp} + wasi bridge.
 */
#include "pymergetic/metal/net/ntp.h"

#include "pymergetic/metal/net/dns.h"
#include "pymergetic/metal/net/udp.h"

#include <stddef.h>
#include <string.h>

#define PM_METAL_NET_NTP_DELTA 2208988800ULL
#define PM_METAL_NET_NTP_ADDR_MAX 8

static uint32_t pm_metal_net_ntp_ntohl(uint32_t net)
{
	return ((net & 0xff000000u) >> 24) | ((net & 0x00ff0000u) >> 8) | ((net & 0x0000ff00u) << 8)
	       | ((net & 0x000000ffu) << 24);
}

int pm_metal_net_ntp_sync(const char *server_host, int32_t timeout_ms, uint64_t *out_unix)
{
	const char *host = (server_host && server_host[0]) ? server_host : "pool.ntp.org";
	pm_metal_net_addr_t addrs[PM_METAL_NET_NTP_ADDR_MAX];
	size_t naddr = 0;
	size_t i;
	uint8_t packet[48];
	uint32_t rsp_len = 0;
	uint32_t secs;
	int fd = -1;

	if (!out_unix) {
		return -1;
	}
	if (pm_metal_net_dns_lookup(host, 123, addrs, PM_METAL_NET_NTP_ADDR_MAX, &naddr) != 0) {
		return -1;
	}

	memset(packet, 0, sizeof(packet));
	packet[0] = 0x23; /* LI=0 VN=4 Mode=3 */

	for (i = 0; i < naddr; i++) {
		fd = pm_metal_net_udp_open(addrs[i].family);
		if (fd < 0) {
			continue;
		}
		if (pm_metal_net_udp_set_timeout_ms(fd, timeout_ms) != 0) {
			(void)pm_metal_net_udp_close(fd);
			fd = -1;
			continue;
		}
		if (pm_metal_net_udp_sendto(fd, packet, (uint32_t)sizeof(packet), &addrs[i]) != 0) {
			(void)pm_metal_net_udp_close(fd);
			fd = -1;
			continue;
		}
		if (pm_metal_net_udp_recv(fd, packet, (uint32_t)sizeof(packet), &rsp_len) != 0) {
			(void)pm_metal_net_udp_close(fd);
			fd = -1;
			continue;
		}
		(void)pm_metal_net_udp_close(fd);
		fd = -1;
		break;
	}

	if (rsp_len < 48) {
		return -1;
	}
	memcpy(&secs, packet + 40, sizeof(secs));
	secs = pm_metal_net_ntp_ntohl(secs);
	if (secs < PM_METAL_NET_NTP_DELTA) {
		return -1;
	}
	*out_unix = (uint64_t)secs - PM_METAL_NET_NTP_DELTA;
	return 0;
}

#include "wasm_export.h"

static int32_t pm_metal_net_ntp_sync_native(wasm_exec_env_t exec_env, const char *server_host,
					      int32_t timeout_ms, uint64_t *out_unix)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_ntp_sync(server_host, timeout_ms, out_unix);
}

static NativeSymbol g_pm_metal_net_ntp_native_symbols[] = {
	{"pm_metal_net_ntp_sync", (void *)pm_metal_net_ntp_sync_native, "($i*)i", NULL},
};

int pm_metal_net_ntp_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_NET_NTP_WASI_MODULE, g_pm_metal_net_ntp_native_symbols,
					    sizeof(g_pm_metal_net_ntp_native_symbols)
						    / sizeof(g_pm_metal_net_ntp_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
