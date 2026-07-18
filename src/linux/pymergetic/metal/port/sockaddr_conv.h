/*
 * Plat-private: pm_metal_net_addr_t ↔ POSIX sockaddr (linux).
 */
#ifndef PYMERGETIC_METAL_PORT_SOCKADDR_CONV_H_
#define PYMERGETIC_METAL_PORT_SOCKADDR_CONV_H_

#include "pymergetic/metal/net/addr.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

static inline void pm_metal_port_addr_from_sockaddr(pm_metal_net_addr_t *out, const struct sockaddr *sa)
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

static inline int pm_metal_port_addr_to_sockaddr(const pm_metal_net_addr_t *a, struct sockaddr_storage *ss,
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

static inline int pm_metal_port_af_from_family(uint8_t family)
{
	if (family == PM_METAL_NET_AF_INET) {
		return AF_INET;
	}
	if (family == PM_METAL_NET_AF_INET6) {
		return AF_INET6;
	}
	return -1;
}

static inline int pm_metal_port_set_sock_timeout_ms(int fd, int32_t timeout_ms)
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

#endif /* PYMERGETIC_METAL_PORT_SOCKADDR_CONV_H_ */
