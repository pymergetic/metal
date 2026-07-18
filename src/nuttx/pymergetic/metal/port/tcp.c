/*
 * Port TCP — nuttx bind (POSIX).
 */
#include "pymergetic/metal/port/tcp.h"

#include "sockaddr_conv.h"

#include <unistd.h>

int pm_metal_port_tcp_open(uint8_t family)
{
	int af = pm_metal_port_af_from_family(family);

	if (af < 0) {
		return -1;
	}
	return socket(af, SOCK_STREAM, IPPROTO_TCP);
}

int pm_metal_port_tcp_close(int fd)
{
	if (fd < 0) {
		return -1;
	}
	return close(fd) == 0 ? 0 : -1;
}

int pm_metal_port_tcp_set_timeout_ms(int fd, int32_t timeout_ms)
{
	return pm_metal_port_set_sock_timeout_ms(fd, timeout_ms);
}

int pm_metal_port_tcp_bind(int fd, const pm_metal_net_addr_t *addr)
{
	struct sockaddr_storage ss;
	socklen_t slen;

	if (fd < 0 || !addr) {
		return -1;
	}
	if (pm_metal_port_addr_to_sockaddr(addr, &ss, &slen) != 0) {
		return -1;
	}
	return bind(fd, (struct sockaddr *)&ss, slen) == 0 ? 0 : -1;
}

int pm_metal_port_tcp_listen(int fd, int backlog)
{
	if (fd < 0) {
		return -1;
	}
	return listen(fd, backlog > 0 ? backlog : 1) == 0 ? 0 : -1;
}

int pm_metal_port_tcp_accept(int fd, pm_metal_net_addr_t *out_peer)
{
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(ss);
	int nfd;

	if (fd < 0) {
		return -1;
	}
	nfd = accept(fd, (struct sockaddr *)&ss, &slen);
	if (nfd < 0) {
		return -1;
	}
	if (out_peer) {
		pm_metal_port_addr_from_sockaddr(out_peer, (struct sockaddr *)&ss);
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
	if (pm_metal_port_addr_to_sockaddr(to, &ss, &slen) != 0) {
		return -1;
	}
	return connect(fd, (struct sockaddr *)&ss, slen) == 0 ? 0 : -1;
}

int pm_metal_port_tcp_send(int fd, const void *buf, uint32_t len, uint32_t *out_sent)
{
	ssize_t n;

	if (fd < 0 || !buf || len == 0) {
		return -1;
	}
	n = send(fd, buf, len, 0);
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
	n = recv(fd, buf, cap, 0);
	if (n < 0) {
		return -1;
	}
	*out_len = (uint32_t)n;
	return 0;
}
