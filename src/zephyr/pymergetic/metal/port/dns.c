/*
 * Port DNS — zephyr bind (zsock_getaddrinfo).
 */
#include "pymergetic/metal/port/dns.h"

#include <string.h>

#if !defined(CONFIG_NET_SOCKETS)

int pm_metal_port_dns_lookup(const char *host, uint16_t port, pm_metal_net_addr_t *out, size_t out_cap,
			     size_t *out_n)
{
	(void)host;
	(void)port;
	(void)out;
	(void)out_cap;
	(void)out_n;
	return -1;
}

#else /* CONFIG_NET_SOCKETS */

#include <stdio.h>
#include <zephyr/net/socket.h>

static void pm_metal_port_addr_from_zsock(pm_metal_net_addr_t *out, const struct sockaddr *sa)
{
	memset(out, 0, sizeof(*out));
	if (sa->sa_family == NET_AF_INET) {
		const struct sockaddr_in *in = (const struct sockaddr_in *)sa;

		out->family = PM_METAL_NET_AF_INET;
		out->port = ntohs(in->sin_port);
		memcpy(out->ip, &in->sin_addr.s_addr, 4);
	} else if (sa->sa_family == NET_AF_INET6) {
		const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;

		out->family = PM_METAL_NET_AF_INET6;
		out->port = ntohs(in6->sin6_port);
		memcpy(out->ip, &in6->sin6_addr.s6_addr, 16);
	}
}

int pm_metal_port_dns_lookup(const char *host, uint16_t port, pm_metal_net_addr_t *out, size_t out_cap,
			     size_t *out_n)
{
	char portstr[8];
	struct zsock_addrinfo hints;
	struct zsock_addrinfo *res = NULL;
	struct zsock_addrinfo *ai;
	size_t n = 0;
	int ret;

	if (!host || !host[0] || !out || out_cap == 0 || !out_n) {
		return -1;
	}
	(void)snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = NET_AF_UNSPEC;
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;
	ret = zsock_getaddrinfo(host, portstr, &hints, &res);
	if (ret != 0 || !res) {
		return -1;
	}
	for (ai = res; ai && n < out_cap; ai = ai->ai_next) {
		if (ai->ai_family != NET_AF_INET && ai->ai_family != NET_AF_INET6) {
			continue;
		}
		pm_metal_port_addr_from_zsock(&out[n], ai->ai_addr);
		if (out[n].family == 0) {
			continue;
		}
		n++;
	}
	zsock_freeaddrinfo(res);
	if (n == 0) {
		return -1;
	}
	*out_n = n;
	return 0;
}

#endif /* CONFIG_NET_SOCKETS */
