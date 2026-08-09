#ifndef PYMERGETIC_METAL_NET_IP_SOCK_H_
#define PYMERGETIC_METAL_NET_IP_SOCK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canon L4 face — all TCP/UDP I/O goes through these socks.
 * STREAM: socket → listen|connect → accept/send/recv → close
 * DGRAM:  socket → bind → sendto/recvfrom → close
 * Optional bind_if pins to named lo/ethN/wgN.
 */
typedef uint32_t pm_metal_net_ip_sock_h;

#define PM_METAL_NET_IP_SOCK_INVALID 0u
#define PM_METAL_NET_IP_AF_INET 1u
#define PM_METAL_NET_IP_AF_INET6 2u
#define PM_METAL_NET_IP_SOCK_STREAM 1u
#define PM_METAL_NET_IP_SOCK_DGRAM 2u

pm_metal_net_ip_sock_h pm_metal_net_ip_socket(uint32_t domain, uint32_t type);
void pm_metal_net_ip_close(pm_metal_net_ip_sock_h h);
int32_t pm_metal_net_ip_bind(pm_metal_net_ip_sock_h h, uint32_t port);
int32_t pm_metal_net_ip_bind_if(pm_metal_net_ip_sock_h h, const char *ifname);

/* Async: park handle; result via pm_metal_async_result_u32 after DONE. */
uint32_t pm_metal_net_ip_connect(pm_metal_net_ip_sock_h h, const char *host, uint32_t port);
uint32_t pm_metal_net_ip_listen(pm_metal_net_ip_sock_h h, uint32_t port);
uint32_t pm_metal_net_ip_accept(pm_metal_net_ip_sock_h h);
uint32_t pm_metal_net_ip_recv(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len);
uint32_t pm_metal_net_ip_dns_lookup(const char *host);

uint32_t pm_metal_net_ip_send(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len);
uint32_t pm_metal_net_ip_sendto(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len,
                                const char *host, uint32_t port);
uint32_t pm_metal_net_ip_try_recv(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len);
uint32_t pm_metal_net_ip_try_recvfrom(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len,
                                      char *peer_host, uint32_t peer_cap, uint32_t *peer_port);

/* Sync poll helpers for C services (no shared global PCB). */
pm_metal_net_ip_sock_h pm_metal_net_ip_try_accept(pm_metal_net_ip_sock_h listen_h);
int32_t pm_metal_net_ip_sock_connected(pm_metal_net_ip_sock_h h);
int32_t pm_metal_net_ip_sock_listening(pm_metal_net_ip_sock_h h);
int32_t pm_metal_net_ip_sock_peer_closed(pm_metal_net_ip_sock_h h);
uint32_t pm_metal_net_ip_sock_rx_avail(pm_metal_net_ip_sock_h h);

/* Host-order IPv4 helpers for DNS/NTP/TFTP/HTTP client. */
int32_t pm_metal_net_ip_connect_ip4(pm_metal_net_ip_sock_h h, uint32_t dst_ip, uint16_t port);
uint32_t pm_metal_net_ip_sendto_ip4(pm_metal_net_ip_sock_h h, uint32_t dst_ip, uint16_t dst_port,
                                    const void *ptr, uint32_t len);
int32_t pm_metal_net_ip_try_recvfrom_ip4(pm_metal_net_ip_sock_h h, uint32_t *src_ip,
                                         uint16_t *src_port, void *buf, uint32_t cap,
                                         uint32_t *len_out);

static inline uint32_t pm_metal_net_ip_result(uint32_t self_h)
{
    extern uint32_t pm_metal_async_result_u32(uint32_t h);
    return pm_metal_async_result_u32(self_h);
}

#ifdef __cplusplus
}
#endif

#endif
