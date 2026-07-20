/*
 * Socket stubs — EFI has no network stack for WAMR yet.
 * All os_socket_* return -1 / __WASI_ENOTCAPABLE.
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

int
os_socket_create(bh_socket_t *sock, bool is_ipv4, bool is_tcp)
{
    (void)sock;
    (void)is_ipv4;
    (void)is_tcp;
    return -1;
}

int
os_socket_bind(bh_socket_t socket, const char *addr, int *port)
{
    (void)socket;
    (void)addr;
    (void)port;
    return -1;
}

int
os_socket_settimeout(bh_socket_t socket, uint64 timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return -1;
}

int
os_socket_listen(bh_socket_t socket, int max_client)
{
    (void)socket;
    (void)max_client;
    return -1;
}

int
os_socket_accept(bh_socket_t server_sock, bh_socket_t *sock, void *addr,
                 unsigned int *addrlen)
{
    (void)server_sock;
    (void)sock;
    (void)addr;
    (void)addrlen;
    return -1;
}

int
os_socket_connect(bh_socket_t socket, const char *addr, int port)
{
    (void)socket;
    (void)addr;
    (void)port;
    return -1;
}

int
os_socket_recv(bh_socket_t socket, void *buf, unsigned int len)
{
    (void)socket;
    (void)buf;
    (void)len;
    return -1;
}

int
os_socket_recv_from(bh_socket_t socket, void *buf, unsigned int len, int flags,
                    bh_sockaddr_t *src_addr)
{
    (void)socket;
    (void)buf;
    (void)len;
    (void)flags;
    (void)src_addr;
    return -1;
}

int
os_socket_send(bh_socket_t socket, const void *buf, unsigned int len)
{
    (void)socket;
    (void)buf;
    (void)len;
    return -1;
}

int
os_socket_send_to(bh_socket_t socket, const void *buf, unsigned int len,
                  int flags, const bh_sockaddr_t *dest_addr)
{
    (void)socket;
    (void)buf;
    (void)len;
    (void)flags;
    (void)dest_addr;
    return -1;
}

int
os_socket_close(bh_socket_t socket)
{
    (void)socket;
    return -1;
}

__wasi_errno_t
os_socket_shutdown(bh_socket_t socket)
{
    (void)socket;
    return __WASI_ENOTCAPABLE;
}

int
os_socket_inet_network(bool is_ipv4, const char *cp, bh_ip_addr_buffer_t *out)
{
    (void)is_ipv4;
    (void)cp;
    (void)out;
    return -1;
}

int
os_socket_addr_resolve(const char *host, const char *service,
                       uint8_t *hint_is_tcp, uint8_t *hint_is_ipv4,
                       bh_addr_info_t *addr_info, size_t addr_info_size,
                       size_t *max_info_size)
{
    (void)host;
    (void)service;
    (void)hint_is_tcp;
    (void)hint_is_ipv4;
    (void)addr_info;
    (void)addr_info_size;
    (void)max_info_size;
    return -1;
}

int
os_socket_addr_local(bh_socket_t socket, bh_sockaddr_t *sockaddr)
{
    (void)socket;
    (void)sockaddr;
    return -1;
}

int
os_socket_addr_remote(bh_socket_t socket, bh_sockaddr_t *sockaddr)
{
    (void)socket;
    (void)sockaddr;
    return -1;
}

int
os_socket_set_send_buf_size(bh_socket_t socket, size_t bufsiz)
{
    (void)socket;
    (void)bufsiz;
    return -1;
}

int
os_socket_get_send_buf_size(bh_socket_t socket, size_t *bufsiz)
{
    (void)socket;
    (void)bufsiz;
    return -1;
}

int
os_socket_set_recv_buf_size(bh_socket_t socket, size_t bufsiz)
{
    (void)socket;
    (void)bufsiz;
    return -1;
}

int
os_socket_get_recv_buf_size(bh_socket_t socket, size_t *bufsiz)
{
    (void)socket;
    (void)bufsiz;
    return -1;
}

int
os_socket_set_keep_alive(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_keep_alive(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_set_send_timeout(bh_socket_t socket, uint64 timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return -1;
}

int
os_socket_get_send_timeout(bh_socket_t socket, uint64 *timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return -1;
}

int
os_socket_set_recv_timeout(bh_socket_t socket, uint64 timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return -1;
}

int
os_socket_get_recv_timeout(bh_socket_t socket, uint64 *timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return -1;
}

int
os_socket_set_reuse_addr(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_reuse_addr(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_set_reuse_port(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_reuse_port(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_set_linger(bh_socket_t socket, bool is_enabled, int linger_s)
{
    (void)socket;
    (void)is_enabled;
    (void)linger_s;
    return -1;
}

int
os_socket_get_linger(bh_socket_t socket, bool *is_enabled, int *linger_s)
{
    (void)socket;
    (void)is_enabled;
    (void)linger_s;
    return -1;
}

int
os_socket_set_tcp_no_delay(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_tcp_no_delay(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_set_tcp_quick_ack(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_tcp_quick_ack(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_set_tcp_keep_idle(bh_socket_t socket, uint32_t time_s)
{
    (void)socket;
    (void)time_s;
    return -1;
}

int
os_socket_get_tcp_keep_idle(bh_socket_t socket, uint32_t *time_s)
{
    (void)socket;
    (void)time_s;
    return -1;
}

int
os_socket_set_tcp_keep_intvl(bh_socket_t socket, uint32_t time_s)
{
    (void)socket;
    (void)time_s;
    return -1;
}

int
os_socket_get_tcp_keep_intvl(bh_socket_t socket, uint32_t *time_s)
{
    (void)socket;
    (void)time_s;
    return -1;
}

int
os_socket_set_tcp_fastopen_connect(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_tcp_fastopen_connect(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_set_ip_multicast_loop(bh_socket_t socket, bool ipv6, bool is_enabled)
{
    (void)socket;
    (void)ipv6;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_ip_multicast_loop(bh_socket_t socket, bool ipv6, bool *is_enabled)
{
    (void)socket;
    (void)ipv6;
    (void)is_enabled;
    return -1;
}

int
os_socket_set_ip_add_membership(bh_socket_t socket,
                                bh_ip_addr_buffer_t *imr_multiaddr,
                                uint32_t imr_interface, bool is_ipv6)
{
    (void)socket;
    (void)imr_multiaddr;
    (void)imr_interface;
    (void)is_ipv6;
    return -1;
}

int
os_socket_set_ip_drop_membership(bh_socket_t socket,
                                 bh_ip_addr_buffer_t *imr_multiaddr,
                                 uint32_t imr_interface, bool is_ipv6)
{
    (void)socket;
    (void)imr_multiaddr;
    (void)imr_interface;
    (void)is_ipv6;
    return -1;
}

int
os_socket_set_ip_ttl(bh_socket_t socket, uint8_t ttl_s)
{
    (void)socket;
    (void)ttl_s;
    return -1;
}

int
os_socket_get_ip_ttl(bh_socket_t socket, uint8_t *ttl_s)
{
    (void)socket;
    (void)ttl_s;
    return -1;
}

int
os_socket_set_ip_multicast_ttl(bh_socket_t socket, uint8_t ttl_s)
{
    (void)socket;
    (void)ttl_s;
    return -1;
}

int
os_socket_get_ip_multicast_ttl(bh_socket_t socket, uint8_t *ttl_s)
{
    (void)socket;
    (void)ttl_s;
    return -1;
}

int
os_socket_set_ipv6_only(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_ipv6_only(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_set_broadcast(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}

int
os_socket_get_broadcast(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return -1;
}
