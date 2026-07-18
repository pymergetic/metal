/*
 * Port DNS — linux bind (getaddrinfo).
 */
#include "pymergetic/metal/port/dns.h"

#include "sockaddr_conv.h"

#include <netdb.h>
#include <stdio.h>
#include <string.h>

int pm_metal_port_dns_lookup(const char *host, uint16_t port, pm_metal_net_addr_t *out, size_t out_cap,
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
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;
	if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
		return -1;
	}
	for (ai = res; ai && n < out_cap; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
			continue;
		}
		pm_metal_port_addr_from_sockaddr(&out[n], ai->ai_addr);
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
