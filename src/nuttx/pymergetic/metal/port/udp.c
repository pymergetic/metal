/*
 * Port UDP — sim: host sockets; else NuttX POSIX.
 */
#include "pymergetic/metal/port/udp.h"

#include <nuttx/config.h>

#if defined(CONFIG_ARCH_SIM)

int pm_metal_host_udp_open(uint8_t family);
int pm_metal_host_udp_close(int fd);
int pm_metal_host_udp_set_timeout_ms(int fd, int32_t timeout_ms);
int pm_metal_host_udp_sendto(int fd, const void *buf, uint32_t len, const pm_metal_net_addr_t *to);
int pm_metal_host_udp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len);

int pm_metal_port_udp_open(uint8_t family)
{
	return pm_metal_host_udp_open(family);
}

int pm_metal_port_udp_close(int fd)
{
	return pm_metal_host_udp_close(fd);
}

int pm_metal_port_udp_set_timeout_ms(int fd, int32_t timeout_ms)
{
	return pm_metal_host_udp_set_timeout_ms(fd, timeout_ms);
}

int pm_metal_port_udp_sendto(int fd, const void *buf, uint32_t len, const pm_metal_net_addr_t *to)
{
	return pm_metal_host_udp_sendto(fd, buf, len, to);
}

int pm_metal_port_udp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
{
	return pm_metal_host_udp_recv(fd, buf, cap, out_len);
}

#else /* !CONFIG_ARCH_SIM */

#include "sockaddr_conv.h"

#include <unistd.h>

int pm_metal_port_udp_open(uint8_t family)
{
	int af = pm_metal_port_af_from_family(family);

	if (af < 0) {
		return -1;
	}
	return socket(af, SOCK_DGRAM, IPPROTO_UDP);
}

int pm_metal_port_udp_close(int fd)
{
	if (fd < 0) {
		return -1;
	}
	return close(fd) == 0 ? 0 : -1;
}

int pm_metal_port_udp_set_timeout_ms(int fd, int32_t timeout_ms)
{
	return pm_metal_port_set_sock_timeout_ms(fd, timeout_ms);
}

int pm_metal_port_udp_sendto(int fd, const void *buf, uint32_t len, const pm_metal_net_addr_t *to)
{
	struct sockaddr_storage ss;
	socklen_t slen;

	if (fd < 0 || !buf || len == 0 || !to) {
		return -1;
	}
	if (pm_metal_port_addr_to_sockaddr(to, &ss, &slen) != 0) {
		return -1;
	}
	if (sendto(fd, buf, len, 0, (struct sockaddr *)&ss, slen) != (ssize_t)len) {
		return -1;
	}
	return 0;
}

int pm_metal_port_udp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
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

#endif /* CONFIG_ARCH_SIM */
