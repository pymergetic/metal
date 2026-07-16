/*
 * SPDX-License-Identifier: Apache-2.0
 * Zephyr WAMR WASI host sockets — metal synthetic handles over zsock_*.
 *
 * Handle model mirrors pipes: bh_socket_t is PM_METAL_SOCK_FD_BASE+i, not a
 * raw zsock fd (incompatible with file.c desc_array).
 */
#include "platform_api_extension.h"
#include "libc_errno.h"

#include "pymergetic/metal/wasi/socket.h"
#include "pymergetic/metal/port/pipe_zephyr.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>

#if defined(CONFIG_NET_SOCKETS)

#include <zephyr/net/socket.h>
#include <zephyr/net/socket_types.h>

#define PM_METAL_SOCK_FD_BASE 2000
#define PM_METAL_SOCK_MAX 16

typedef struct {
	int zfd;
	int is_tcp; /* 1 stream, 0 dgram */
	int used;
	int closing;
	int refs; /* in-flight ops; close waits until 0 before zsock_close */
} pm_metal_sock_t;

static pm_metal_sock_t g_pm_metal_socks[PM_METAL_SOCK_MAX];
K_MUTEX_DEFINE(g_pm_metal_sock_table_lock);

struct pm_metal_linger {
	int l_onoff;
	int l_linger;
};

static int
pm_metal_sock_index(int handle)
{
	int idx;

	if (handle < PM_METAL_SOCK_FD_BASE) {
		return -1;
	}
	idx = handle - PM_METAL_SOCK_FD_BASE;
	if (idx < 0 || idx >= PM_METAL_SOCK_MAX) {
		return -1;
	}
	return idx;
}

/* Acquire a live slot for an op (bumps refs). Pair with pm_metal_sock_release. */
static int
pm_metal_sock_acquire(bh_socket_t handle, int *out_zfd)
{
	int idx = pm_metal_sock_index((int)handle);

	if (idx < 0 || out_zfd == NULL) {
		errno = EBADF;
		return BHT_ERROR;
	}
	k_mutex_lock(&g_pm_metal_sock_table_lock, K_FOREVER);
	if (!g_pm_metal_socks[idx].used || g_pm_metal_socks[idx].closing) {
		k_mutex_unlock(&g_pm_metal_sock_table_lock);
		errno = EBADF;
		return BHT_ERROR;
	}
	g_pm_metal_socks[idx].refs++;
	*out_zfd = g_pm_metal_socks[idx].zfd;
	k_mutex_unlock(&g_pm_metal_sock_table_lock);
	return BHT_OK;
}

static void
pm_metal_sock_release(bh_socket_t handle)
{
	int idx = pm_metal_sock_index((int)handle);
	int zfd_close = -1;

	if (idx < 0) {
		return;
	}
	k_mutex_lock(&g_pm_metal_sock_table_lock, K_FOREVER);
	if (g_pm_metal_socks[idx].refs > 0) {
		g_pm_metal_socks[idx].refs--;
	}
	if (g_pm_metal_socks[idx].closing && g_pm_metal_socks[idx].refs == 0) {
		zfd_close = g_pm_metal_socks[idx].zfd;
		g_pm_metal_socks[idx].used = 0;
		g_pm_metal_socks[idx].closing = 0;
		g_pm_metal_socks[idx].zfd = -1;
		g_pm_metal_socks[idx].is_tcp = 0;
	}
	k_mutex_unlock(&g_pm_metal_sock_table_lock);
	if (zfd_close >= 0) {
		if (zsock_close(zfd_close) != 0) {
			printk("pm_metal_wasi_socket: zsock_close(%d) failed errno=%d\n",
			       zfd_close, errno);
		}
	}
}

/* Legacy name: acquire for an op (callers must pm_metal_sock_release). */
static int
pm_metal_sock_lookup_zfd(bh_socket_t handle, int *out_zfd)
{
	return pm_metal_sock_acquire(handle, out_zfd);
}

static int
pm_metal_sock_alloc(int zfd, int is_tcp, bh_socket_t *out)
{
	int i;

	if (out == NULL || zfd < 0) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	k_mutex_lock(&g_pm_metal_sock_table_lock, K_FOREVER);
	for (i = 0; i < PM_METAL_SOCK_MAX; i++) {
		if (!g_pm_metal_socks[i].used) {
			g_pm_metal_socks[i].zfd = zfd;
			g_pm_metal_socks[i].is_tcp = is_tcp ? 1 : 0;
			g_pm_metal_socks[i].used = 1;
			g_pm_metal_socks[i].closing = 0;
			g_pm_metal_socks[i].refs = 0;
			*out = (bh_socket_t)(PM_METAL_SOCK_FD_BASE + i);
			k_mutex_unlock(&g_pm_metal_sock_table_lock);
			return BHT_OK;
		}
	}
	k_mutex_unlock(&g_pm_metal_sock_table_lock);
	errno = EMFILE;
	return BHT_ERROR;
}

int
pm_metal_wasi_socket_is_ours(int handle)
{
	int idx = pm_metal_sock_index(handle);
	int ours = 0;

	if (idx < 0) {
		return 0;
	}
	k_mutex_lock(&g_pm_metal_sock_table_lock, K_FOREVER);
	/* Include closing slots: callers that gate on is_ours() must not fall
	 * through to the file fd table while refs remain (SOCK-9). Ops still
	 * go through acquire(), which rejects closing with EBADF.
	 */
	ours = g_pm_metal_socks[idx].used;
	k_mutex_unlock(&g_pm_metal_sock_table_lock);
	return ours;
}

int
pm_metal_wasi_socket_is_tcp(int handle)
{
	int idx = pm_metal_sock_index(handle);
	int is_tcp = -1;

	if (idx < 0) {
		return -1;
	}
	k_mutex_lock(&g_pm_metal_sock_table_lock, K_FOREVER);
	if (g_pm_metal_socks[idx].used) {
		is_tcp = g_pm_metal_socks[idx].is_tcp;
	}
	k_mutex_unlock(&g_pm_metal_sock_table_lock);
	return is_tcp;
}

int
pm_metal_wasi_socket_ioctl_fionread(int handle, int *avail)
{
	int zfd;
	int ret;

	if (avail == NULL) {
		errno = EBADF;
		return -1;
	}
	if (pm_metal_sock_acquire((bh_socket_t)handle, &zfd) != BHT_OK) {
		return -1;
	}
	ret = zsock_ioctl(zfd, ZFD_IOCTL_FIONREAD, avail);
	pm_metal_sock_release((bh_socket_t)handle);
	return ret;
}

static bool
textual_addr_to_sockaddr(const char *textual, int port, struct net_sockaddr *out,
			 net_socklen_t *out_len)
{
	struct net_sockaddr_in *v4;
#ifdef CONFIG_NET_IPV6
	struct net_sockaddr_in6 *v6;
#endif

	if (textual == NULL || out == NULL || out_len == NULL) {
		return false;
	}

	v4 = (struct net_sockaddr_in *)out;
	if (zsock_inet_pton(NET_AF_INET, textual, &v4->sin_addr) == 1) {
		v4->sin_family = NET_AF_INET;
		v4->sin_port = net_htons((uint16_t)port);
		*out_len = sizeof(struct net_sockaddr_in);
		return true;
	}

#ifdef CONFIG_NET_IPV6
	v6 = (struct net_sockaddr_in6 *)out;
	if (zsock_inet_pton(NET_AF_INET6, textual, &v6->sin6_addr) == 1) {
		v6->sin6_family = NET_AF_INET6;
		v6->sin6_port = net_htons((uint16_t)port);
		*out_len = sizeof(struct net_sockaddr_in6);
		return true;
	}
#endif

	return false;
}

static int
sockaddr_to_bh_sockaddr(const struct net_sockaddr *sockaddr,
			bh_sockaddr_t *bh_sockaddr)
{
	switch (sockaddr->sa_family) {
	case NET_AF_INET: {
		struct net_sockaddr_in *addr = (struct net_sockaddr_in *)sockaddr;

		bh_sockaddr->port = net_ntohs(addr->sin_port);
		bh_sockaddr->addr_buffer.ipv4 = net_ntohl(addr->sin_addr.s_addr);
		bh_sockaddr->is_ipv4 = true;
		return BHT_OK;
	}
#ifdef CONFIG_NET_IPV6
	case NET_AF_INET6: {
		struct net_sockaddr_in6 *addr =
			(struct net_sockaddr_in6 *)sockaddr;
		size_t i;

		bh_sockaddr->port = net_ntohs(addr->sin6_port);
		for (i = 0; i < sizeof(bh_sockaddr->addr_buffer.ipv6)
					/ sizeof(bh_sockaddr->addr_buffer.ipv6[0]);
		     i++) {
			uint16_t part_addr =
				addr->sin6_addr.s6_addr[i * 2]
				| (addr->sin6_addr.s6_addr[i * 2 + 1] << 8);
			bh_sockaddr->addr_buffer.ipv6[i] = net_ntohs(part_addr);
		}
		bh_sockaddr->is_ipv4 = false;
		return BHT_OK;
	}
#endif
	default:
		errno = EAFNOSUPPORT;
		return BHT_ERROR;
	}
}

static void
bh_sockaddr_to_sockaddr(const bh_sockaddr_t *bh_sockaddr,
			struct net_sockaddr_storage *sockaddr,
			net_socklen_t *socklen)
{
	if (bh_sockaddr->is_ipv4) {
		struct net_sockaddr_in *addr = (struct net_sockaddr_in *)sockaddr;

		addr->sin_port = net_htons(bh_sockaddr->port);
		addr->sin_family = NET_AF_INET;
		addr->sin_addr.s_addr = net_htonl(bh_sockaddr->addr_buffer.ipv4);
		*socklen = sizeof(*addr);
	}
#ifdef CONFIG_NET_IPV6
	else {
		struct net_sockaddr_in6 *addr =
			(struct net_sockaddr_in6 *)sockaddr;
		size_t i;

		addr->sin6_port = net_htons(bh_sockaddr->port);
		addr->sin6_family = NET_AF_INET6;
		for (i = 0; i < sizeof(bh_sockaddr->addr_buffer.ipv6)
					/ sizeof(bh_sockaddr->addr_buffer.ipv6[0]);
		     i++) {
			uint16_t part_addr =
				net_htons(bh_sockaddr->addr_buffer.ipv6[i]);
			addr->sin6_addr.s6_addr[i * 2] = 0xff & part_addr;
			addr->sin6_addr.s6_addr[i * 2 + 1] =
				(0xff00 & part_addr) >> 8;
		}
		*socklen = sizeof(*addr);
	}
#else
	else {
		memset(sockaddr, 0, sizeof(*sockaddr));
		*socklen = 0;
	}
#endif
}

static int
os_socket_setbooloption(bh_socket_t socket, int level, int optname,
			bool is_enabled)
{
	int zfd;
	int option = (int)is_enabled;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_setsockopt(zfd, level, optname, &option, sizeof(option))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

static int
os_socket_getbooloption(bh_socket_t socket, int level, int optname,
			bool *is_enabled)
{
	int zfd;
	int optval;
	net_socklen_t optval_size = sizeof(optval);

	if (is_enabled == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, level, optname, &optval, &optval_size) != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	*is_enabled = (bool)optval;
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_create(bh_socket_t *sock, bool is_ipv4, bool is_tcp)
{
	int af;
	int zfd;

	if (sock == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}

	af = is_ipv4 ? NET_AF_INET : NET_AF_INET6;
#ifndef CONFIG_NET_IPV6
	if (!is_ipv4) {
		errno = EAFNOSUPPORT;
		return BHT_ERROR;
	}
#endif

	if (is_tcp) {
		zfd = zsock_socket(af, NET_SOCK_STREAM, NET_IPPROTO_TCP);
	} else {
		zfd = zsock_socket(af, NET_SOCK_DGRAM, 0);
	}
	if (zfd < 0) {
		return BHT_ERROR;
	}

	if (pm_metal_sock_alloc(zfd, is_tcp ? 1 : 0, sock) != BHT_OK) {
		if (zsock_close(zfd) != 0) {
			printk("pm_metal_wasi_socket: zsock_close(%d) after alloc fail errno=%d\n",
			       zfd, errno);
		}
		return BHT_ERROR;
	}
	return BHT_OK;
}

int
os_socket_bind(bh_socket_t socket, const char *addr, int *port)
{
	struct net_sockaddr_storage addr_storage = { 0 };
	struct pm_metal_linger ling;
	net_socklen_t socklen;
	int zfd;
	int ret;

	if (addr == NULL || port == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}

	if (!textual_addr_to_sockaddr(addr, *port,
				      (struct net_sockaddr *)&addr_storage,
				      &socklen)) {
		errno = EINVAL;
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}

	/* Skip Linux-only fcntl(FD_CLOEXEC). */
	/* SO_LINGER is TCP-only; skip for UDP (SOCK-6). */
	if (pm_metal_wasi_socket_is_tcp((int)socket) == 1) {
		ling.l_onoff = 1;
		ling.l_linger = 0;
		ret = zsock_setsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_LINGER,
				       &ling, sizeof(ling));
		if (ret < 0) {
			pm_metal_sock_release(socket);
			return BHT_ERROR;
		}
	}

	ret = zsock_bind(zfd, (struct net_sockaddr *)&addr_storage, socklen);
	if (ret < 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}

	socklen = sizeof(addr_storage);
	if (zsock_getsockname(zfd, (struct net_sockaddr *)&addr_storage,
			      &socklen)
	    == -1) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}

	if (addr_storage.ss_family == NET_AF_INET) {
		*port = net_ntohs(
			((struct net_sockaddr_in *)&addr_storage)->sin_port);
	} else {
#ifdef CONFIG_NET_IPV6
		*port = net_ntohs(
			((struct net_sockaddr_in6 *)&addr_storage)->sin6_port);
#else
		errno = EAFNOSUPPORT;
		pm_metal_sock_release(socket);
		return BHT_ERROR;
#endif
	}

	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_settimeout(bh_socket_t socket, uint64 timeout_us)
{
	struct timeval tv;
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	tv.tv_sec = (time_t)(timeout_us / 1000000UL);
	tv.tv_usec = (suseconds_t)(timeout_us % 1000000UL);
	if (zsock_setsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVTIMEO, &tv,
			     sizeof(tv))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_listen(bh_socket_t socket, int max_client)
{
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_listen(zfd, max_client) != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_accept(bh_socket_t server_sock, bh_socket_t *sock, void *addr,
		 unsigned int *addrlen)
{
	int server_zfd;
	int new_zfd;
	bh_socket_t metal;

	if (sock == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	/*
	 * Match WAMR posix_socket.c: addr == NULL skips peer address output;
	 * non-NULL addr requires a valid addrlen in/out (POSIX accept).
	 */
	if (addr != NULL && addrlen == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(server_sock, &server_zfd) != BHT_OK) {
		return BHT_ERROR;
	}

	if (addr == NULL) {
		new_zfd = zsock_accept(server_zfd, NULL, NULL);
	} else {
		net_socklen_t len = (net_socklen_t)*addrlen;

		new_zfd = zsock_accept(server_zfd, addr, &len);
		*addrlen = (unsigned int)len;
	}
	if (new_zfd < 0) {
		pm_metal_sock_release(server_sock);
		return BHT_ERROR;
	}

	/* accept() is TCP only */
	if (pm_metal_sock_alloc(new_zfd, 1, &metal) != BHT_OK) {
		if (zsock_close(new_zfd) != 0) {
			printk("pm_metal_wasi_socket: zsock_close(%d) after accept alloc fail errno=%d\n",
			       new_zfd, errno);
		}
		pm_metal_sock_release(server_sock);
		return BHT_ERROR;
	}
	*sock = metal;
	pm_metal_sock_release(server_sock);
	return BHT_OK;
}

int
os_socket_connect(bh_socket_t socket, const char *addr, int port)
{
	struct net_sockaddr_storage addr_in = { 0 };
	net_socklen_t addr_len;
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (!textual_addr_to_sockaddr(addr, port,
				      (struct net_sockaddr *)&addr_in,
				      &addr_len)) {
		errno = EINVAL;
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	if (zsock_connect(zfd, (struct net_sockaddr *)&addr_in, addr_len)
	    == -1) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_recv(bh_socket_t socket, void *buf, unsigned int len)
{
	int zfd;
	int ret;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return -1;
	}
	ret = (int)zsock_recv(zfd, buf, len, 0);
	pm_metal_sock_release(socket);
	return ret;
}

int
os_socket_recv_from(bh_socket_t socket, void *buf, unsigned int len, int flags,
		    bh_sockaddr_t *src_addr)
{
	struct net_sockaddr_storage sock_addr = { 0 };
	net_socklen_t socklen = sizeof(sock_addr);
	int zfd;
	int ret;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return -1;
	}

	ret = (int)zsock_recvfrom(zfd, buf, len, flags,
				  (struct net_sockaddr *)&sock_addr, &socklen);
	if (ret < 0) {
		pm_metal_sock_release(socket);
		return ret;
	}

	if (src_addr && socklen > 0) {
		if (sockaddr_to_bh_sockaddr((struct net_sockaddr *)&sock_addr,
					    src_addr)
		    == BHT_ERROR) {
			pm_metal_sock_release(socket);
			return -1;
		}
	} else if (src_addr) {
		memset(src_addr, 0, sizeof(*src_addr));
	}

	pm_metal_sock_release(socket);
	return ret;
}

int
os_socket_send(bh_socket_t socket, const void *buf, unsigned int len)
{
	int zfd;
	int ret;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return -1;
	}
	ret = (int)zsock_send(zfd, buf, len, 0);
	pm_metal_sock_release(socket);
	return ret;
}

int
os_socket_send_to(bh_socket_t socket, const void *buf, unsigned int len,
		  int flags, const bh_sockaddr_t *dest_addr)
{
	struct net_sockaddr_storage sock_addr = { 0 };
	net_socklen_t socklen = 0;
	int zfd;

	if (dest_addr == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return -1;
	}

	bh_sockaddr_to_sockaddr(dest_addr, &sock_addr, &socklen);
	{
		int ret = (int)zsock_sendto(zfd, buf, len, flags,
					    (const struct net_sockaddr *)&sock_addr,
					    socklen);

		pm_metal_sock_release(socket);
		return ret;
	}
}

int
os_socket_close(bh_socket_t socket)
{
	int idx = pm_metal_sock_index((int)socket);
	int zfd_close = -1;

	if (idx < 0) {
		errno = EBADF;
		return BHT_ERROR;
	}
	k_mutex_lock(&g_pm_metal_sock_table_lock, K_FOREVER);
	if (!g_pm_metal_socks[idx].used || g_pm_metal_socks[idx].closing) {
		k_mutex_unlock(&g_pm_metal_sock_table_lock);
		errno = EBADF;
		return BHT_ERROR;
	}
	g_pm_metal_socks[idx].closing = 1;
	if (g_pm_metal_socks[idx].refs == 0) {
		zfd_close = g_pm_metal_socks[idx].zfd;
		g_pm_metal_socks[idx].used = 0;
		g_pm_metal_socks[idx].closing = 0;
		g_pm_metal_socks[idx].zfd = -1;
		g_pm_metal_socks[idx].is_tcp = 0;
	}
	k_mutex_unlock(&g_pm_metal_sock_table_lock);
	if (zfd_close >= 0) {
		if (zsock_close(zfd_close) != 0) {
			printk("pm_metal_wasi_socket: zsock_close(%d) on close errno=%d\n",
			       zfd_close, errno);
			return BHT_ERROR;
		}
	}
	return BHT_OK;
}

__wasi_errno_t
os_socket_shutdown(bh_socket_t socket)
{
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return convert_errno(errno);
	}
	if (zsock_shutdown(zfd, ZSOCK_SHUT_RDWR) != 0) {
		pm_metal_sock_release(socket);
		return convert_errno(errno);
	}
	pm_metal_sock_release(socket);
	return __WASI_ESUCCESS;
}

int
os_socket_inet_network(bool is_ipv4, const char *cp, bh_ip_addr_buffer_t *out)
{
	if (cp == NULL || out == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}

	if (is_ipv4) {
		struct net_in_addr addr;

		if (zsock_inet_pton(NET_AF_INET, cp, &addr) != 1) {
			return BHT_ERROR;
		}
		out->ipv4 = net_ntohl(addr.s_addr);
	} else {
#ifdef CONFIG_NET_IPV6
		struct net_in6_addr addr;
		int i;

		if (zsock_inet_pton(NET_AF_INET6, cp, &addr) != 1) {
			return BHT_ERROR;
		}
		for (i = 0; i < 8; i++) {
			uint16_t part =
				addr.s6_addr[i * 2]
				| ((uint16_t)addr.s6_addr[i * 2 + 1] << 8);
			out->ipv6[i] = net_ntohs(part);
		}
#else
		errno = EAFNOSUPPORT;
		return BHT_ERROR;
#endif
	}

	return BHT_OK;
}

static int
getaddrinfo_error_to_errno(int error)
{
	switch (error) {
	case DNS_EAI_AGAIN:
		return EAGAIN;
	case DNS_EAI_FAIL:
		return EFAULT;
	case DNS_EAI_MEMORY:
		return ENOMEM;
	case DNS_EAI_SYSTEM:
		return errno;
	default:
		return EINVAL;
	}
}

static int
is_addrinfo_supported(struct zsock_addrinfo *info)
{
	return (info->ai_family == NET_AF_INET || info->ai_family == NET_AF_INET6)
	       && (info->ai_socktype == NET_SOCK_DGRAM
		   || info->ai_socktype == NET_SOCK_STREAM)
	       && (info->ai_protocol == NET_IPPROTO_TCP
		   || info->ai_protocol == NET_IPPROTO_UDP
		   || info->ai_protocol == 0);
}

int
os_socket_addr_resolve(const char *host, const char *service,
		       uint8_t *hint_is_tcp, uint8_t *hint_is_ipv4,
		       bh_addr_info_t *addr_info, size_t addr_info_size,
		       size_t *max_info_size)
{
	struct zsock_addrinfo hints = { 0 }, *res, *result;
	int hints_enabled = hint_is_tcp || hint_is_ipv4;
	int ret;
	size_t pos = 0;

	if (max_info_size == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}

	/*
	 * Offline / loopback-only Zephyr has no real DNS — short-circuit the
	 * two names ns_lookup_pool tests use so sock_addr_resolve() never
	 * depends on zsock_getaddrinfo finding a resolver.
	 */
	if (host != NULL
	    && (strcmp(host, "localhost") == 0
		|| strcmp(host, "ip6-localhost") == 0)) {
		int want_ipv4 = (strcmp(host, "localhost") == 0);
		bool is_tcp = true;
		uint16_t port = 0;

		if (hint_is_ipv4) {
			want_ipv4 = *hint_is_ipv4 ? 1 : 0;
		}
		if (hint_is_tcp) {
			is_tcp = *hint_is_tcp ? true : false;
		}
		if (service != NULL && service[0] != '\0') {
			port = (uint16_t)atoi(service);
		}

		if (addr_info != NULL && addr_info_size > 0) {
			memset(&addr_info[0], 0, sizeof(addr_info[0]));
			addr_info[0].is_tcp = is_tcp;
			addr_info[0].sockaddr.port = port;
			if (want_ipv4) {
				addr_info[0].sockaddr.is_ipv4 = true;
				addr_info[0].sockaddr.addr_buffer.ipv4 =
					0x7f000001u; /* 127.0.0.1 */
			} else {
				addr_info[0].sockaddr.is_ipv4 = false;
				/* ::1 in host-order 16-bit parts */
				addr_info[0].sockaddr.addr_buffer.ipv6[7] = 1;
			}
		}
		*max_info_size = 1;
		return BHT_OK;
	}

	if (hints_enabled) {
		if (hint_is_ipv4) {
			hints.ai_family =
				*hint_is_ipv4 ? NET_AF_INET : NET_AF_INET6;
		}
		if (hint_is_tcp) {
			hints.ai_socktype =
				*hint_is_tcp ? NET_SOCK_STREAM : NET_SOCK_DGRAM;
		}
	}

	ret = zsock_getaddrinfo(host,
				(service != NULL && strlen(service) == 0)
					? NULL
					: service,
				hints_enabled ? &hints : NULL, &result);
	if (ret != 0) {
		errno = getaddrinfo_error_to_errno(ret);
		return BHT_ERROR;
	}

	res = result;
	while (res) {
		if (!is_addrinfo_supported(res)) {
			res = res->ai_next;
			continue;
		}
		if (addr_info != NULL && addr_info_size > pos) {
			ret = sockaddr_to_bh_sockaddr(res->ai_addr,
						      &addr_info[pos].sockaddr);
			if (ret == BHT_ERROR) {
				zsock_freeaddrinfo(result);
				return BHT_ERROR;
			}
			addr_info[pos].is_tcp =
				res->ai_socktype == NET_SOCK_STREAM;
		}
		pos++;
		res = res->ai_next;
	}

	*max_info_size = pos;
	zsock_freeaddrinfo(result);
	return BHT_OK;
}

int
os_socket_addr_local(bh_socket_t socket, bh_sockaddr_t *sockaddr)
{
	struct net_sockaddr_storage addr_storage = { 0 };
	net_socklen_t addr_len = sizeof(addr_storage);
	int zfd;

	if (sockaddr == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockname(zfd, (struct net_sockaddr *)&addr_storage,
			      &addr_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return sockaddr_to_bh_sockaddr((struct net_sockaddr *)&addr_storage,
				       sockaddr);
}

int
os_socket_addr_remote(bh_socket_t socket, bh_sockaddr_t *sockaddr)
{
	struct net_sockaddr_storage addr_storage = { 0 };
	net_socklen_t addr_len = sizeof(addr_storage);
	int zfd;

	if (sockaddr == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getpeername(zfd, (struct net_sockaddr *)&addr_storage,
			      &addr_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return sockaddr_to_bh_sockaddr((struct net_sockaddr *)&addr_storage,
				       sockaddr);
}

int
os_socket_set_send_buf_size(bh_socket_t socket, size_t bufsiz)
{
	int zfd;
	int buf_size_int = (int)bufsiz;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_setsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_SNDBUF,
			     &buf_size_int, sizeof(buf_size_int))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_send_buf_size(bh_socket_t socket, size_t *bufsiz)
{
	int zfd;
	int buf_size_int;
	net_socklen_t bufsiz_len = sizeof(buf_size_int);

	if (bufsiz == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_SNDBUF,
			     &buf_size_int, &bufsiz_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	*bufsiz = (size_t)buf_size_int;
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_recv_buf_size(bh_socket_t socket, size_t bufsiz)
{
	int zfd;
	int buf_size_int = (int)bufsiz;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_setsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVBUF,
			     &buf_size_int, sizeof(buf_size_int))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_recv_buf_size(bh_socket_t socket, size_t *bufsiz)
{
	int zfd;
	int buf_size_int;
	net_socklen_t bufsiz_len = sizeof(buf_size_int);

	if (bufsiz == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVBUF,
			     &buf_size_int, &bufsiz_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	*bufsiz = (size_t)buf_size_int;
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_keep_alive(bh_socket_t socket, bool is_enabled)
{
	return os_socket_setbooloption(socket, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_KEEPALIVE, is_enabled);
}

int
os_socket_get_keep_alive(bh_socket_t socket, bool *is_enabled)
{
	return os_socket_getbooloption(socket, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_KEEPALIVE, is_enabled);
}

int
os_socket_set_send_timeout(bh_socket_t socket, uint64 timeout_us)
{
	struct timeval tv;
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	tv.tv_sec = (time_t)(timeout_us / 1000000UL);
	tv.tv_usec = (suseconds_t)(timeout_us % 1000000UL);
	if (zsock_setsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_SNDTIMEO, &tv,
			     sizeof(tv))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_send_timeout(bh_socket_t socket, uint64 *timeout_us)
{
	struct timeval tv;
	net_socklen_t tv_len = sizeof(tv);
	int zfd;

	if (timeout_us == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_SNDTIMEO, &tv,
			     &tv_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	*timeout_us = ((uint64)tv.tv_sec * 1000000UL) + (uint64)tv.tv_usec;
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_recv_timeout(bh_socket_t socket, uint64 timeout_us)
{
	struct timeval tv;
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	tv.tv_sec = (time_t)(timeout_us / 1000000UL);
	tv.tv_usec = (suseconds_t)(timeout_us % 1000000UL);
	if (zsock_setsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVTIMEO, &tv,
			     sizeof(tv))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_recv_timeout(bh_socket_t socket, uint64 *timeout_us)
{
	struct timeval tv;
	net_socklen_t tv_len = sizeof(tv);
	int zfd;

	if (timeout_us == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVTIMEO, &tv,
			     &tv_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	*timeout_us = ((uint64)tv.tv_sec * 1000000UL) + (uint64)tv.tv_usec;
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_reuse_addr(bh_socket_t socket, bool is_enabled)
{
	return os_socket_setbooloption(socket, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_REUSEADDR, is_enabled);
}

int
os_socket_get_reuse_addr(bh_socket_t socket, bool *is_enabled)
{
	return os_socket_getbooloption(socket, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_REUSEADDR, is_enabled);
}

int
os_socket_set_reuse_port(bh_socket_t socket, bool is_enabled)
{
	return os_socket_setbooloption(socket, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_REUSEPORT, is_enabled);
}

int
os_socket_get_reuse_port(bh_socket_t socket, bool *is_enabled)
{
	return os_socket_getbooloption(socket, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_REUSEPORT, is_enabled);
}

int
os_socket_set_linger(bh_socket_t socket, bool is_enabled, int linger_s)
{
	struct pm_metal_linger linger_opts = { .l_onoff = (int)is_enabled,
					       .l_linger = linger_s };
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	/* SO_LINGER is meaningless on UDP — no-op success. */
	if (pm_metal_wasi_socket_is_tcp((int)socket) != 1) {
		pm_metal_sock_release(socket);
		return BHT_OK;
	}
	if (zsock_setsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_LINGER,
			     &linger_opts, sizeof(linger_opts))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_linger(bh_socket_t socket, bool *is_enabled, int *linger_s)
{
	struct pm_metal_linger linger_opts;
	net_socklen_t linger_opts_len = sizeof(linger_opts);
	int zfd;

	if (is_enabled == NULL || linger_s == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, ZSOCK_SOL_SOCKET, ZSOCK_SO_LINGER,
			     &linger_opts, &linger_opts_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	*linger_s = linger_opts.l_linger;
	*is_enabled = (bool)linger_opts.l_onoff;
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_tcp_no_delay(bh_socket_t socket, bool is_enabled)
{
	return os_socket_setbooloption(socket, NET_IPPROTO_TCP,
				       ZSOCK_TCP_NODELAY, is_enabled);
}

int
os_socket_get_tcp_no_delay(bh_socket_t socket, bool *is_enabled)
{
	return os_socket_getbooloption(socket, NET_IPPROTO_TCP,
				       ZSOCK_TCP_NODELAY, is_enabled);
}

int
os_socket_set_tcp_quick_ack(bh_socket_t socket, bool is_enabled)
{
	(void)socket;
	(void)is_enabled;
	errno = ENOTSUP;
	return BHT_ERROR;
}

int
os_socket_get_tcp_quick_ack(bh_socket_t socket, bool *is_enabled)
{
	(void)socket;
	(void)is_enabled;
	errno = ENOTSUP;
	return BHT_ERROR;
}

int
os_socket_set_tcp_keep_idle(bh_socket_t socket, uint32_t time_s)
{
	int zfd;
	int time_s_int = (int)time_s;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_setsockopt(zfd, NET_IPPROTO_TCP, ZSOCK_TCP_KEEPIDLE,
			     &time_s_int, sizeof(time_s_int))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_tcp_keep_idle(bh_socket_t socket, uint32_t *time_s)
{
	int zfd;
	int time_s_int;
	net_socklen_t time_s_len = sizeof(time_s_int);

	if (time_s == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, NET_IPPROTO_TCP, ZSOCK_TCP_KEEPIDLE,
			     &time_s_int, &time_s_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	*time_s = (uint32_t)time_s_int;
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_tcp_keep_intvl(bh_socket_t socket, uint32_t time_s)
{
	int zfd;
	int time_s_int = (int)time_s;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_setsockopt(zfd, NET_IPPROTO_TCP, ZSOCK_TCP_KEEPINTVL,
			     &time_s_int, sizeof(time_s_int))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_tcp_keep_intvl(bh_socket_t socket, uint32_t *time_s)
{
	int zfd;
	int time_s_int;
	net_socklen_t time_s_len = sizeof(time_s_int);

	if (time_s == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, NET_IPPROTO_TCP, ZSOCK_TCP_KEEPINTVL,
			     &time_s_int, &time_s_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	*time_s = (uint32_t)time_s_int;
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_tcp_fastopen_connect(bh_socket_t socket, bool is_enabled)
{
	(void)socket;
	(void)is_enabled;
	errno = ENOTSUP;
	return BHT_ERROR;
}

int
os_socket_get_tcp_fastopen_connect(bh_socket_t socket, bool *is_enabled)
{
	(void)socket;
	(void)is_enabled;
	errno = ENOTSUP;
	return BHT_ERROR;
}

int
os_socket_set_ip_multicast_loop(bh_socket_t socket, bool ipv6, bool is_enabled)
{
	if (ipv6) {
#ifdef CONFIG_NET_IPV6
		return os_socket_setbooloption(socket, NET_IPPROTO_IPV6,
					       ZSOCK_IPV6_MULTICAST_LOOP,
					       is_enabled);
#else
		(void)socket;
		(void)is_enabled;
		errno = EAFNOSUPPORT;
		return BHT_ERROR;
#endif
	}
	return os_socket_setbooloption(socket, NET_IPPROTO_IP,
				       ZSOCK_IP_MULTICAST_LOOP, is_enabled);
}

int
os_socket_get_ip_multicast_loop(bh_socket_t socket, bool ipv6, bool *is_enabled)
{
	if (ipv6) {
#ifdef CONFIG_NET_IPV6
		return os_socket_getbooloption(socket, NET_IPPROTO_IPV6,
					       ZSOCK_IPV6_MULTICAST_LOOP,
					       is_enabled);
#else
		(void)socket;
		(void)is_enabled;
		errno = EAFNOSUPPORT;
		return BHT_ERROR;
#endif
	}
	return os_socket_getbooloption(socket, NET_IPPROTO_IP,
				       ZSOCK_IP_MULTICAST_LOOP, is_enabled);
}

int
os_socket_set_ip_add_membership(bh_socket_t socket,
				bh_ip_addr_buffer_t *imr_multiaddr,
				uint32_t imr_interface, bool is_ipv6)
{
	int zfd;

	if (imr_multiaddr == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}

	if (is_ipv6) {
#ifdef CONFIG_NET_IPV6
		struct net_ipv6_mreq mreq;
		int i;

		/* Host-order bh buffer → network order (same as bind/connect). */
		for (i = 0; i < 8; i++) {
			uint16_t part_addr = net_htons(imr_multiaddr->ipv6[i]);

			mreq.ipv6mr_multiaddr.s6_addr[i * 2] = 0xff & part_addr;
			mreq.ipv6mr_multiaddr.s6_addr[i * 2 + 1] =
				(0xff00 & part_addr) >> 8;
		}
		mreq.ipv6mr_ifindex = (int)imr_interface;
		if (zsock_setsockopt(zfd, NET_IPPROTO_IPV6,
				     ZSOCK_IPV6_ADD_MEMBERSHIP, &mreq,
				     sizeof(mreq))
		    != 0) {
			pm_metal_sock_release(socket);
			return BHT_ERROR;
		}
#else
		errno = EAFNOSUPPORT;
		pm_metal_sock_release(socket);
		return BHT_ERROR;
#endif
	} else {
		struct net_ip_mreq mreq;

		mreq.imr_multiaddr.s_addr = net_htonl(imr_multiaddr->ipv4);
		mreq.imr_interface.s_addr = net_htonl(imr_interface);
		if (zsock_setsockopt(zfd, NET_IPPROTO_IP,
				     ZSOCK_IP_ADD_MEMBERSHIP, &mreq,
				     sizeof(mreq))
		    != 0) {
			pm_metal_sock_release(socket);
			return BHT_ERROR;
		}
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_ip_drop_membership(bh_socket_t socket,
				 bh_ip_addr_buffer_t *imr_multiaddr,
				 uint32_t imr_interface, bool is_ipv6)
{
	int zfd;

	if (imr_multiaddr == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}

	if (is_ipv6) {
#ifdef CONFIG_NET_IPV6
		struct net_ipv6_mreq mreq;
		int i;

		for (i = 0; i < 8; i++) {
			uint16_t part_addr = net_htons(imr_multiaddr->ipv6[i]);

			mreq.ipv6mr_multiaddr.s6_addr[i * 2] = 0xff & part_addr;
			mreq.ipv6mr_multiaddr.s6_addr[i * 2 + 1] =
				(0xff00 & part_addr) >> 8;
		}
		mreq.ipv6mr_ifindex = (int)imr_interface;
		if (zsock_setsockopt(zfd, NET_IPPROTO_IPV6,
				     ZSOCK_IPV6_DROP_MEMBERSHIP, &mreq,
				     sizeof(mreq))
		    != 0) {
			pm_metal_sock_release(socket);
			return BHT_ERROR;
		}
#else
		errno = EAFNOSUPPORT;
		pm_metal_sock_release(socket);
		return BHT_ERROR;
#endif
	} else {
		struct net_ip_mreq mreq;

		mreq.imr_multiaddr.s_addr = net_htonl(imr_multiaddr->ipv4);
		mreq.imr_interface.s_addr = net_htonl(imr_interface);
		if (zsock_setsockopt(zfd, NET_IPPROTO_IP,
				     ZSOCK_IP_DROP_MEMBERSHIP, &mreq,
				     sizeof(mreq))
		    != 0) {
			pm_metal_sock_release(socket);
			return BHT_ERROR;
		}
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_ip_ttl(bh_socket_t socket, uint8_t ttl_s)
{
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_setsockopt(zfd, NET_IPPROTO_IP, ZSOCK_IP_TTL, &ttl_s,
			     sizeof(ttl_s))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_ip_ttl(bh_socket_t socket, uint8_t *ttl_s)
{
	int zfd;
	net_socklen_t opt_len = sizeof(*ttl_s);

	if (ttl_s == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, NET_IPPROTO_IP, ZSOCK_IP_TTL, ttl_s, &opt_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_ip_multicast_ttl(bh_socket_t socket, uint8_t ttl_s)
{
	int zfd;

	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_setsockopt(zfd, NET_IPPROTO_IP, ZSOCK_IP_MULTICAST_TTL,
			     &ttl_s, sizeof(ttl_s))
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_get_ip_multicast_ttl(bh_socket_t socket, uint8_t *ttl_s)
{
	int zfd;
	net_socklen_t opt_len = sizeof(*ttl_s);

	if (ttl_s == NULL) {
		errno = EINVAL;
		return BHT_ERROR;
	}
	if (pm_metal_sock_lookup_zfd(socket, &zfd) != BHT_OK) {
		return BHT_ERROR;
	}
	if (zsock_getsockopt(zfd, NET_IPPROTO_IP, ZSOCK_IP_MULTICAST_TTL, ttl_s,
			     &opt_len)
	    != 0) {
		pm_metal_sock_release(socket);
		return BHT_ERROR;
	}
	pm_metal_sock_release(socket);
	return BHT_OK;
}

int
os_socket_set_broadcast(bh_socket_t socket, bool is_enabled)
{
	return os_socket_setbooloption(socket, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_BROADCAST, is_enabled);
}

int
os_socket_get_broadcast(bh_socket_t socket, bool *is_enabled)
{
	return os_socket_getbooloption(socket, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_BROADCAST, is_enabled);
}

int
os_socket_set_ipv6_only(bh_socket_t socket, bool is_enabled)
{
#ifdef CONFIG_NET_IPV6
	return os_socket_setbooloption(socket, NET_IPPROTO_IPV6,
				       ZSOCK_IPV6_V6ONLY, is_enabled);
#else
	(void)socket;
	(void)is_enabled;
	errno = EAFNOSUPPORT;
	return BHT_ERROR;
#endif
}

int
os_socket_get_ipv6_only(bh_socket_t socket, bool *is_enabled)
{
#ifdef CONFIG_NET_IPV6
	return os_socket_getbooloption(socket, NET_IPPROTO_IPV6,
				       ZSOCK_IPV6_V6ONLY, is_enabled);
#else
	(void)socket;
	(void)is_enabled;
	errno = EAFNOSUPPORT;
	return BHT_ERROR;
#endif
}

#ifndef PM_METAL_WASI_POLL_MAX
#define PM_METAL_WASI_POLL_MAX 16
#endif

/*
 * Metal poll: translate socket handles to zsock fds under acquire, evaluate
 * pipe readiness via the virtual pipe table, and reject other synthetic fds
 * (WASI file handles) with POLLNVAL — they are not in Zephyr's fd table.
 */
int
pm_metal_wasi_socket_poll(void *fds_v, int nfds, int timeout)
{
	struct {
		int fd;
		short events;
		short revents;
	} *fds = fds_v;
	struct zsock_pollfd zfds[PM_METAL_WASI_POLL_MAX];
	bh_socket_t acquired[PM_METAL_WASI_POLL_MAX];
	int sock_map[PM_METAL_WASI_POLL_MAX];
	int nsock = 0;
	int npipe = 0;
	int i;
	int ready;
	int64_t deadline_ms = -1;
	int slice;

	if (fds == NULL && nfds > 0) {
		errno = EFAULT;
		return -1;
	}
	if (nfds < 0 || nfds > PM_METAL_WASI_POLL_MAX) {
		errno = EINVAL;
		return -1;
	}

	for (i = 0; i < nfds; i++) {
		fds[i].revents = 0;
	}

	for (i = 0; i < nfds; i++) {
		int zfd;

		if (fds[i].fd < 0) {
			continue;
		}
		if (pm_metal_wasi_socket_is_ours(fds[i].fd)) {
			if (pm_metal_sock_acquire((bh_socket_t)fds[i].fd, &zfd)
			    != BHT_OK) {
				fds[i].revents = ZSOCK_POLLNVAL;
				continue;
			}
			zfds[nsock].fd = zfd;
			zfds[nsock].events = fds[i].events;
			zfds[nsock].revents = 0;
			acquired[nsock] = (bh_socket_t)fds[i].fd;
			sock_map[nsock] = i;
			nsock++;
		} else if (pm_metal_port_pipe_is_ours(fds[i].fd)) {
			npipe++;
		} else {
			/* File / unknown synthetic handles are not zsock fds. */
			fds[i].revents = ZSOCK_POLLNVAL;
		}
	}

	if (timeout > 0) {
		deadline_ms = k_uptime_get() + timeout;
	}

	for (;;) {
		ready = 0;

		for (i = 0; i < nfds; i++) {
			if (fds[i].fd < 0) {
				continue;
			}
			if (pm_metal_port_pipe_is_ours(fds[i].fd)) {
				fds[i].revents = pm_metal_port_pipe_poll_revents(
					fds[i].fd, fds[i].events);
			}
			if (fds[i].revents != 0) {
				ready++;
			}
		}

		if (nsock > 0) {
			if (ready > 0 || timeout == 0) {
				slice = 0;
			} else if (timeout < 0) {
				/* Slice when pipes are also watched so we can
				 * observe pipe readiness without a socket event.
				 */
				slice = (npipe > 0) ? 10 : -1;
			} else {
				int64_t left = deadline_ms - k_uptime_get();

				if (left <= 0) {
					slice = 0;
				} else if (npipe > 0 && left > 10) {
					slice = 10;
				} else if (left > (int64_t)INT_MAX) {
					slice = INT_MAX;
				} else {
					slice = (int)left;
				}
			}

			if (zsock_poll(zfds, nsock, slice) < 0) {
				for (i = 0; i < nsock; i++) {
					pm_metal_sock_release(acquired[i]);
				}
				return -1;
			}
			for (i = 0; i < nsock; i++) {
				int fi = sock_map[i];

				fds[fi].revents = zfds[i].revents;
				if (fds[fi].revents != 0) {
					ready++;
				}
			}
		} else if (ready == 0 && timeout != 0) {
			if (timeout < 0) {
				k_sleep(K_MSEC(10));
			} else {
				int64_t left = deadline_ms - k_uptime_get();

				if (left <= 0) {
					break;
				}
				k_sleep(K_MSEC(left < 10 ? left : 10));
			}
			continue;
		}

		if (ready > 0 || timeout == 0) {
			break;
		}
		if (timeout > 0 && k_uptime_get() >= deadline_ms) {
			break;
		}
		/* SOCK-8: timeout < 0 must not break on a zero-ready wakeup
		 * (zsock_poll(..., -1) returning 0). Loop and retry.
		 */
	}

	for (i = 0; i < nsock; i++) {
		pm_metal_sock_release(acquired[i]);
	}

	ready = 0;
	for (i = 0; i < nfds; i++) {
		if (fds[i].revents != 0) {
			ready++;
		}
	}
	return ready;
}

#else /* !CONFIG_NET_SOCKETS */

int
pm_metal_wasi_socket_is_ours(int handle)
{
	(void)handle;
	return 0;
}

int
pm_metal_wasi_socket_is_tcp(int handle)
{
	(void)handle;
	return -1;
}

int
pm_metal_wasi_socket_poll(void *fds, int nfds, int timeout)
{
	(void)fds;
	(void)nfds;
	(void)timeout;
	errno = ENOSYS;
	return -1;
}

int
pm_metal_wasi_socket_ioctl_fionread(int handle, int *avail)
{
	(void)handle;
	(void)avail;
	errno = ENOSYS;
	return -1;
}

#define PM_WASI_SOCK_STUB(name, args)                                                                \
	int name args                                                                                \
	{                                                                                            \
		errno = ENOSYS;                                                                      \
		return BHT_ERROR;                                                                    \
	}

PM_WASI_SOCK_STUB(os_socket_create, (bh_socket_t *sock, bool is_ipv4, bool is_tcp))
PM_WASI_SOCK_STUB(os_socket_bind, (bh_socket_t socket, const char *addr, int *port))
PM_WASI_SOCK_STUB(os_socket_settimeout, (bh_socket_t socket, uint64 timeout_us))
PM_WASI_SOCK_STUB(os_socket_listen, (bh_socket_t socket, int max_client))
PM_WASI_SOCK_STUB(os_socket_accept,
		  (bh_socket_t server_sock, bh_socket_t *sock, void *addr, unsigned int *addrlen))
PM_WASI_SOCK_STUB(os_socket_connect, (bh_socket_t socket, const char *addr, int port))
PM_WASI_SOCK_STUB(os_socket_recv, (bh_socket_t socket, void *buf, unsigned int len))
PM_WASI_SOCK_STUB(os_socket_recv_from,
		  (bh_socket_t socket, void *buf, unsigned int len, int flags,
		   bh_sockaddr_t *src_addr))
PM_WASI_SOCK_STUB(os_socket_send, (bh_socket_t socket, const void *buf, unsigned int len))
PM_WASI_SOCK_STUB(os_socket_send_to,
		  (bh_socket_t socket, const void *buf, unsigned int len, int flags,
		   const bh_sockaddr_t *dest_addr))
PM_WASI_SOCK_STUB(os_socket_close, (bh_socket_t socket))

__wasi_errno_t
os_socket_shutdown(bh_socket_t socket)
{
	(void)socket;
	return __WASI_ENOSYS;
}

PM_WASI_SOCK_STUB(os_socket_inet_network, (bool is_ipv4, const char *cp, bh_ip_addr_buffer_t *out))
PM_WASI_SOCK_STUB(os_socket_addr_resolve,
		  (const char *host, const char *service, uint8_t *hint_is_tcp,
		   uint8_t *hint_is_ipv4, bh_addr_info_t *addr_info, size_t addr_info_size,
		   size_t *max_info_size))
PM_WASI_SOCK_STUB(os_socket_addr_local, (bh_socket_t socket, bh_sockaddr_t *sockaddr))
PM_WASI_SOCK_STUB(os_socket_addr_remote, (bh_socket_t socket, bh_sockaddr_t *sockaddr))
PM_WASI_SOCK_STUB(os_socket_set_send_buf_size, (bh_socket_t socket, size_t bufsiz))
PM_WASI_SOCK_STUB(os_socket_get_send_buf_size, (bh_socket_t socket, size_t *bufsiz))
PM_WASI_SOCK_STUB(os_socket_set_recv_buf_size, (bh_socket_t socket, size_t bufsiz))
PM_WASI_SOCK_STUB(os_socket_get_recv_buf_size, (bh_socket_t socket, size_t *bufsiz))
PM_WASI_SOCK_STUB(os_socket_set_keep_alive, (bh_socket_t socket, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_keep_alive, (bh_socket_t socket, bool *is_enabled))
PM_WASI_SOCK_STUB(os_socket_set_send_timeout, (bh_socket_t socket, uint64 timeout_us))
PM_WASI_SOCK_STUB(os_socket_get_send_timeout, (bh_socket_t socket, uint64 *timeout_us))
PM_WASI_SOCK_STUB(os_socket_get_recv_timeout, (bh_socket_t socket, uint64 *timeout_us))
PM_WASI_SOCK_STUB(os_socket_set_recv_timeout, (bh_socket_t socket, uint64 timeout_us))
PM_WASI_SOCK_STUB(os_socket_set_reuse_addr, (bh_socket_t socket, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_reuse_addr, (bh_socket_t socket, bool *is_enabled))
PM_WASI_SOCK_STUB(os_socket_set_reuse_port, (bh_socket_t socket, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_reuse_port, (bh_socket_t socket, bool *is_enabled))
PM_WASI_SOCK_STUB(os_socket_set_linger, (bh_socket_t socket, bool is_enabled, int linger_s))
PM_WASI_SOCK_STUB(os_socket_get_linger, (bh_socket_t socket, bool *is_enabled, int *linger_s))
PM_WASI_SOCK_STUB(os_socket_set_tcp_no_delay, (bh_socket_t socket, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_tcp_no_delay, (bh_socket_t socket, bool *is_enabled))
PM_WASI_SOCK_STUB(os_socket_set_tcp_quick_ack, (bh_socket_t socket, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_tcp_quick_ack, (bh_socket_t socket, bool *is_enabled))
PM_WASI_SOCK_STUB(os_socket_set_tcp_keep_idle, (bh_socket_t socket, uint32_t time_s))
PM_WASI_SOCK_STUB(os_socket_get_tcp_keep_idle, (bh_socket_t socket, uint32_t *time_s))
PM_WASI_SOCK_STUB(os_socket_set_tcp_keep_intvl, (bh_socket_t socket, uint32_t time_s))
PM_WASI_SOCK_STUB(os_socket_get_tcp_keep_intvl, (bh_socket_t socket, uint32_t *time_s))
PM_WASI_SOCK_STUB(os_socket_set_tcp_fastopen_connect, (bh_socket_t socket, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_tcp_fastopen_connect, (bh_socket_t socket, bool *is_enabled))
PM_WASI_SOCK_STUB(os_socket_set_ip_multicast_loop, (bh_socket_t socket, bool ipv6, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_ip_multicast_loop,
		  (bh_socket_t socket, bool ipv6, bool *is_enabled))
PM_WASI_SOCK_STUB(os_socket_set_ip_add_membership,
		  (bh_socket_t socket, bh_ip_addr_buffer_t *imr_multiaddr, uint32_t imr_interface,
		   bool is_ipv6))
PM_WASI_SOCK_STUB(os_socket_set_ip_drop_membership,
		  (bh_socket_t socket, bh_ip_addr_buffer_t *imr_multiaddr, uint32_t imr_interface,
		   bool is_ipv6))
PM_WASI_SOCK_STUB(os_socket_set_ip_ttl, (bh_socket_t socket, uint8_t ttl_s))
PM_WASI_SOCK_STUB(os_socket_get_ip_ttl, (bh_socket_t socket, uint8_t *ttl_s))
PM_WASI_SOCK_STUB(os_socket_set_ip_multicast_ttl, (bh_socket_t socket, uint8_t ttl_s))
PM_WASI_SOCK_STUB(os_socket_get_ip_multicast_ttl, (bh_socket_t socket, uint8_t *ttl_s))
PM_WASI_SOCK_STUB(os_socket_set_broadcast, (bh_socket_t socket, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_broadcast, (bh_socket_t socket, bool *is_enabled))
PM_WASI_SOCK_STUB(os_socket_set_ipv6_only, (bh_socket_t socket, bool is_enabled))
PM_WASI_SOCK_STUB(os_socket_get_ipv6_only, (bh_socket_t socket, bool *is_enabled))

#undef PM_WASI_SOCK_STUB

#endif /* CONFIG_NET_SOCKETS */
