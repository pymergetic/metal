#ifndef PM_METAL_IP_LWIP_INTERNAL_H_
#define PM_METAL_IP_LWIP_INTERNAL_H_

#include <stdint.h>
#include <string.h>

#include "lwipopts.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

#include "pymergetic/metal/net/ip/cfg.h"
#include "pymergetic/metal/net/ip/lwip_start.h"
#include "pymergetic/metal/net/ip/sock.h"

#define METAL_NET_MAX_IFACES PM_METAL_NET_IP_MAX_IFS
#define METAL_NET_MAX_SOCKS 16u
#define METAL_NET_ACCEPT_Q 8u
#define METAL_UDP_RX_MAX 32u
#define METAL_NET_TX_MAX 1514u

typedef struct {
    int32_t used;
    struct netif netif;
    char name[PM_METAL_NET_IP_IFNAME_MAX];
    char backend[24];
    int (*l2_open)(uint8_t mac[6]);
    const uint8_t *(*l2_mac)(void);
    int (*l2_tx)(const void *frame, uint32_t len);
    pm_metal_net_ip_l2_poll_fn l2_poll;
    char ip[16];
    char mask[16];
    char gw[16];
    char dns[16];
    char ntp[16];
    char tftp[PM_METAL_NET_TFTP_HOST_MAX];
    char boot_file[PM_METAL_NET_IP_BOOT_FILE_MAX];
    int32_t use_dhcp;
} metal_net_iface_t;

typedef struct {
    struct pbuf *p;
    ip_addr_t addr;
    uint16_t port;
} metal_udp_rx_t;

typedef struct {
    int32_t used;
    uint32_t domain;
    uint32_t type;
    struct tcp_pcb *tcp;
    struct udp_pcb *udp;
    ip_addr_t remote;
    uint16_t remote_port;
    int32_t have_remote;
    int32_t conn_done;
    int32_t conn_ok;
    int32_t listening;
    struct tcp_pcb *accept_pcb;
    pm_metal_net_ip_sock_h accept_q[METAL_NET_ACCEPT_Q];
    uint8_t accept_q_r;
    uint8_t accept_q_n;
    void *recv_buf;
    uint32_t recv_cap;
    uint32_t recv_got;
    int32_t recv_done;
    int32_t recv_err;
    struct pbuf *rx_q;
    int32_t bound_if;
    metal_udp_rx_t udp_rx[METAL_UDP_RX_MAX];
    uint16_t udp_rx_head;
    uint16_t udp_rx_n;
    uint32_t wait_connect;
    uint32_t wait_recv;
    uint32_t wait_accept;
    ip_addr_t last_peer;
    uint16_t last_peer_port;
    int32_t have_last_peer;
} msock_t;

extern metal_net_iface_t g_metal_ifaces[METAL_NET_MAX_IFACES];
extern uint32_t g_metal_iface_count;
extern uint32_t g_metal_eth_count;
extern int32_t g_metal_default_idx;
extern uint32_t g_metal_if_gen;
extern int32_t g_metal_lwip_inited;
extern msock_t g_metal_socks[METAL_NET_MAX_SOCKS + 1u];
extern char g_metal_dns_last[64];
extern uint32_t g_metal_if_wait_h;
extern uint32_t g_metal_if_wait_since;

metal_net_iface_t *pm_metal_ip_iface_by_name(const char *name);
metal_net_iface_t *pm_metal_ip_iface_default(void);
void pm_metal_ip_bump_if_gen(void);
void pm_metal_ip_sync_iface(metal_net_iface_t *mif);
int pm_metal_ip_parse_ipv4(const char *s, void *ip4_addr_out);
int pm_metal_ip_parse_host(const char *host, ip_addr_t *out);
void pm_metal_ip_sock_wake_poll(void);

#endif
