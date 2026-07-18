/*
 * Port TCP — zephyr bind (zsock_*).
 */
#include "pymergetic/metal/port/tcp.h"

#include <string.h>

#if !defined(CONFIG_NET_SOCKETS)

int pm_metal_port_tcp_open(uint8_t family)
{
	(void)family;
	return -1;
}

int pm_metal_port_tcp_close(int fd)
{
	(void)fd;
	return -1;
}

int pm_metal_port_tcp_set_timeout_ms(int fd, int32_t timeout_ms)
{
	(void)fd;
	(void)timeout_ms;
	return -1;
}

int pm_metal_port_tcp_bind(int fd, const pm_metal_net_addr_t *addr)
{
	(void)fd;
	(void)addr;
	return -1;
}

int pm_metal_port_tcp_listen(int fd, int backlog)
{
	(void)fd;
	(void)backlog;
	return -1;
}

int pm_metal_port_tcp_accept(int fd, pm_metal_net_addr_t *out_peer)
{
	(void)fd;
	(void)out_peer;
	return -1;
}

int pm_metal_port_tcp_connect(int fd, const pm_metal_net_addr_t *to)
{
	(void)fd;
	(void)to;
	return -1;
}

int pm_metal_port_tcp_send(int fd, const void *buf, uint32_t len, uint32_t *out_sent)
{
	(void)fd;
	(void)buf;
	(void)len;
	(void)out_sent;
	return -1;
}

int pm_metal_port_tcp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
{
	(void)fd;
	(void)buf;
	(void)cap;
	(void)out_len;
	return -1;
}

#else /* CONFIG_NET_SOCKETS */

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

static int pm_metal_port_addr_to_zsock(const pm_metal_net_addr_t *a, struct sockaddr_storage *ss,
					 socklen_t *len)
{
	memset(ss, 0, sizeof(*ss));
	if (a->family == PM_METAL_NET_AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *)ss;

		in->sin_family = NET_AF_INET;
		in->sin_port = htons(a->port);
		memcpy(&in->sin_addr.s_addr, a->ip, 4);
		*len = sizeof(*in);
		return 0;
	}
	if (a->family == PM_METAL_NET_AF_INET6) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ss;

		in6->sin6_family = NET_AF_INET6;
		in6->sin6_port = htons(a->port);
		memcpy(&in6->sin6_addr.s6_addr, a->ip, 16);
		*len = sizeof(*in6);
		return 0;
	}
	return -1;
}

int pm_metal_port_tcp_open(uint8_t family)
{
	int af;

	if (family == PM_METAL_NET_AF_INET) {
		af = NET_AF_INET;
	} else if (family == PM_METAL_NET_AF_INET6) {
		af = NET_AF_INET6;
	} else {
		return -1;
	}
	return zsock_socket(af, NET_SOCK_STREAM, NET_IPPROTO_TCP);
}

int pm_metal_port_tcp_close(int fd)
{
	if (fd < 0) {
		return -1;
	}
	return zsock_close(fd) == 0 ? 0 : -1;
}

int pm_metal_port_tcp_set_timeout_ms(int fd, int32_t timeout_ms)
{
	struct timeval tv;
	int timeout = timeout_ms > 0 ? (int)timeout_ms : 3000;

	if (fd < 0) {
		return -1;
	}
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;
	if (zsock_setsockopt(fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
		return -1;
	}
	if (zsock_setsockopt(fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
		return -1;
	}
	return 0;
}

int pm_metal_port_tcp_bind(int fd, const pm_metal_net_addr_t *addr)
{
	struct sockaddr_storage ss;
	socklen_t slen;

	if (fd < 0 || !addr) {
		return -1;
	}
	if (pm_metal_port_addr_to_zsock(addr, &ss, &slen) != 0) {
		return -1;
	}
	return zsock_bind(fd, (struct sockaddr *)&ss, slen) == 0 ? 0 : -1;
}

int pm_metal_port_tcp_listen(int fd, int backlog)
{
	if (fd < 0) {
		return -1;
	}
	return zsock_listen(fd, backlog > 0 ? backlog : 1) == 0 ? 0 : -1;
}

int pm_metal_port_tcp_accept(int fd, pm_metal_net_addr_t *out_peer)
{
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(ss);
	int nfd;

	if (fd < 0) {
		return -1;
	}
	nfd = zsock_accept(fd, (struct sockaddr *)&ss, &slen);
	if (nfd < 0) {
		return -1;
	}
	if (out_peer) {
		pm_metal_port_addr_from_zsock(out_peer, (struct sockaddr *)&ss);
	}
	return nfd;
}

int pm_metal_port_tcp_connect(int fd, const pm_metal_net_addr_t *to)
{
	struct sockaddr_storage ss;
	socklen_t slen;

	if (fd < 0 || !to) {
		return -1;
	}
	if (pm_metal_port_addr_to_zsock(to, &ss, &slen) != 0) {
		return -1;
	}
	return zsock_connect(fd, (struct sockaddr *)&ss, slen) == 0 ? 0 : -1;
}

int pm_metal_port_tcp_send(int fd, const void *buf, uint32_t len, uint32_t *out_sent)
{
	ssize_t n;

	if (fd < 0 || !buf || len == 0) {
		return -1;
	}
	n = zsock_send(fd, buf, len, 0);
	if (n < 0) {
		return -1;
	}
	if (out_sent) {
		*out_sent = (uint32_t)n;
	}
	return 0;
}

int pm_metal_port_tcp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
{
	ssize_t n;

	if (fd < 0 || !buf || cap == 0 || !out_len) {
		return -1;
	}
	n = zsock_recv(fd, buf, cap, 0);
	if (n < 0) {
		return -1;
	}
	*out_len = (uint32_t)n;
	return 0;
}

#endif /* CONFIG_NET_SOCKETS */
