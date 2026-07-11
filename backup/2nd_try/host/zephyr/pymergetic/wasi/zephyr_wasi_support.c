/*
 * SPDX-License-Identifier: Apache-2.0
 * Zephyr WAMR WASI support — mutex/clock + socket stubs (file-only mods).
 */
#include "libc_errno.h"
#include "platform_api_extension.h"
#include "platform_api_vmcore.h"

#include <zephyr/kernel.h>

#include <time.h>

#define PM_WASI_NS_PER_SEC 1000000000ULL

int
os_mutex_init(korp_mutex *mutex)
{
	if (mutex == NULL) {
		return -1;
	}
	k_mutex_init(mutex);
	return 0;
}

int
os_mutex_lock(korp_mutex *mutex)
{
	if (mutex == NULL) {
		return -1;
	}
	return k_mutex_lock(mutex, K_FOREVER);
}

int
os_mutex_unlock(korp_mutex *mutex)
{
	if (mutex == NULL) {
		return -1;
	}
	return k_mutex_unlock(mutex);
}

static __wasi_errno_t
pm_wasi_clock_value(__wasi_clockid_t clock_id, __wasi_timestamp_t *out)
{
	if (out == NULL) {
		return __WASI_EINVAL;
	}

	switch (clock_id) {
	case __WASI_CLOCK_MONOTONIC:
		*out = ( __wasi_timestamp_t)k_uptime_get() * 1000000ULL;
		return __WASI_ESUCCESS;
	case __WASI_CLOCK_REALTIME: {
		time_t now = time(NULL);

		if (now <= 0) {
#ifdef PM_METAL_BUILT_EPOCH
			*out = ( __wasi_timestamp_t)PM_METAL_BUILT_EPOCH * PM_WASI_NS_PER_SEC;
#else
			*out = 0;
#endif
		} else {
			*out = ( __wasi_timestamp_t)now * PM_WASI_NS_PER_SEC;
		}
		return __WASI_ESUCCESS;
	}
	default:
		return __WASI_ENOTSUP;
	}
}

__wasi_errno_t
os_clock_res_get(__wasi_clockid_t clock_id, __wasi_timestamp_t *resolution)
{
	(void)clock_id;
	if (resolution == NULL) {
		return __WASI_EINVAL;
	}
	*resolution = 1000000ULL;
	return __WASI_ESUCCESS;
}

__wasi_errno_t
os_clock_time_get(__wasi_clockid_t clock_id, __wasi_timestamp_t precision,
		  __wasi_timestamp_t *time)
{
	(void)precision;
	return pm_wasi_clock_value(clock_id, time);
}

#define PM_WASI_SOCK_STUB(name, args)                                                                \
	int name args                                                                                  \
	{                                                                                              \
		(void)0;                                                                               \
		return -1;                                                                             \
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

int
os_rwlock_init(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}

int
os_rwlock_rdlock(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}

int
os_rwlock_wrlock(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}

int
os_rwlock_unlock(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}

int
os_rwlock_destroy(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}
