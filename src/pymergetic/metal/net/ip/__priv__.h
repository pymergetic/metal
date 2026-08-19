/* pymergetic.metal.net.ip — card-private surface shared by the card's own
 * translation units (__impl__, __link__, __wire__, __tcp__). Never included
 * from outside the card: the public border is __exports__.h.
 *
 * The stack keeps one set of tables (sockets, interfaces, routes) that every
 * slice touches, so they live here as extern state defined by __impl__.c.
 * pm_ip_* names are card-internal; only pm_metal_net_ip_* is exported.
 */
#ifndef PYMERGETIC_METAL_NET_IP_PRIV_H
#define PYMERGETIC_METAL_NET_IP_PRIV_H

#include "pymergetic/metal/async.h"
#include "pymergetic/util/lock.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>

#define PM_METAL_IP_SOCK_MAX 32
#define PM_METAL_IP_RX_MAX 8192
#define PM_METAL_IP_PKT_MAX 8232
#define PM_METAL_IP_TCP_MSS (PM_METAL_IP_PKT_MAX - 40u)

/* Initial send window for a fresh TCP socket before we learn the peer's
 * advertised window; matches the peer's receive buffer for a single-hop sim. */
#define SSND_WND_DEFAULT PM_METAL_IP_RX_MAX
#define PM_METAL_IP_LO_BE 0x7f000001u
#define PM_METAL_IP_ACCEPT_MAX 4
#define PM_METAL_IP_REXMIT_MAX 2048
#define PM_METAL_IP_RTO_US 50000ull
#define PM_METAL_IP_L2_MAX 32u
#define PM_METAL_IP_RT_MAX 16u
#define PM_METAL_IP_MASK24 0xffffff00u
#define PM_METAL_IP_ARP_MAX 8u
/* Groups a single UDP socket may join via pm_metal_net_ip_join_group. Zenoh's
 * scout link joins the scouting group on its listener; unicast sockets need none. */
#define PM_METAL_IP_MCAST_MAX 4u
/* One datagram waits per unresolved neighbour: a UDP client gets no retransmit,
 * so dropping its first query would make every one-shot request fail once. */
#define PM_METAL_IP_ARP_PEND 1500u
#define PM_METAL_IP_ARP_TTL_US 60000000ull
#define PM_METAL_IP_ARP_RETRY_US 500000ull
#define PM_METAL_IP_ARP_TRIES 4u
#define PM_METAL_IP_PING_WAIT_US 2000000ull
#define PM_METAL_IP_PING_SPINS 200000u

#define ARP_FREE 0u
#define ARP_ASKING 1u
#define ARP_LIVE 2u

#define SK_UDP 1u
#define SK_TCP 2u

#define TCP_LISTEN 1u
#define TCP_SYN_SENT 2u
#define TCP_SYN_RCVD 3u
#define TCP_ESTAB 4u
#define TCP_FIN_WAIT 5u
#define TCP_CLOSE_WAIT 6u

#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u

struct pm_metal_sock {
    uint32_t used;
    uint8_t kind;
    uint8_t tcp_st;
    uint32_t bound;
    int32_t l2_h;
    uint32_t laddr_be;
    uint16_t lport;
    uint32_t raddr_be;
    uint16_t rport;
    /* Multicast groups this UDP socket has joined (IGMP-style membership).
     * Empty for unicast/loopback sockets. */
    uint32_t mcast_be[PM_METAL_IP_MCAST_MAX];
    uint32_t mcast_n;
    uint32_t snd_nxt;
    uint32_t snd_una;
    uint32_t snd_wnd; /* peer's advertised receive window (in-flight budget) */
    uint32_t rcv_nxt;
    uint32_t iss;
    uint8_t rexmit[PM_METAL_IP_REXMIT_MAX];
    uint32_t rexmit_len;
    uint32_t rexmit_seq;
    uint8_t rexmit_flags;
    uint64_t rexmit_at;
    uint8_t rx[PM_METAL_IP_RX_MAX];
    uint32_t rx_len;
    uint32_t rx_addr_be;
    uint16_t rx_port;
    uint32_t peer_fin;
    pm_metal_async_task_t *waiter;
    int32_t listen_fd;
    int32_t accept_q[PM_METAL_IP_ACCEPT_MAX];
    uint32_t accept_n;
};

struct pm_metal_ip_l2 {
    int32_t h;
    uint32_t addr_be;
    uint32_t mask_be;
};

struct pm_metal_ip_rt {
    uint32_t used;
    uint32_t dst_be;
    uint32_t mask_be;
    /* 0 = on-link: the destination itself is the neighbour to resolve. */
    uint32_t gw_be;
    int32_t h;
};

struct pm_metal_ip_arp {
    uint32_t state;
    int32_t h;
    uint32_t addr_be;
    uint8_t mac[6];
    /* ARP_LIVE: when the entry goes stale. ARP_ASKING: when to ask again. */
    uint64_t at_us;
    uint32_t tries;
    uint8_t pend[PM_METAL_IP_ARP_PEND];
    uint32_t pend_len;
};

/* Defined by __impl__.c. */
extern pm_util_mem_arena_t *pm_ip_arena;
extern uint32_t pm_ip_lo_up;
extern uint32_t pm_ip_lo_addr_be;
extern pm_util_lock_t pm_ip_lock;
extern struct pm_metal_sock pm_ip_sk[PM_METAL_IP_SOCK_MAX];
extern struct pm_metal_ip_l2 pm_ip_l2[PM_METAL_IP_L2_MAX];
extern uint32_t pm_ip_l2_n;
extern int32_t pm_ip_l2_cur;
extern uint32_t pm_ip_if_pending_be;
extern uint32_t pm_ip_if_pending_mask;
extern struct pm_metal_ip_rt pm_ip_rt[PM_METAL_IP_RT_MAX];
extern struct pm_metal_ip_arp pm_ip_arp[PM_METAL_IP_ARP_MAX];
extern int32_t pm_ip_rx_l2;
extern uint8_t pm_ip_ping_out[PM_METAL_IP_RX_MAX];
extern uint32_t pm_ip_ping_len;
extern uint16_t pm_ip_ping_id;

void pm_ip_sock_wake(struct pm_metal_sock *s);
int32_t pm_ip_sock_alloc(uint8_t kind);
/* True if any UDP socket has joined the given multicast group. Lets __wire__.c
 * accept a multicast-destined IPv4 datagram that is not one of our unicast
 * addresses (see ip_input). */
int32_t pm_ip_mcast_joined(uint32_t dst_be);
/* Pump the NIC/TCP timers assuming pm_ip_lock is already held (non-reentrant
 * RS lock: external callers go through pm_metal_net_ip_pump, locked internal
 * flow calls straight here). */
void pm_ip_pump_locked(void);

/* __link__.c — routes, interfaces, ARP. */
void pm_ip_rt_upsert(uint32_t dst_be, uint32_t mask_be, uint32_t gw_be, int32_t h);
void pm_ip_rt_del(uint32_t dst_be, uint32_t mask_be);
void pm_ip_rt_del_h(int32_t h);
void pm_ip_l2_clear(void);
uint32_t pm_ip_l2_addr_of(int32_t h);
uint32_t pm_ip_l2_mask_of(int32_t h);
int32_t pm_ip_l2_has(int32_t h);
int32_t pm_ip_l2_h_for_addr(uint32_t addr_be);
int32_t pm_ip_if_up_mask(int32_t h, uint32_t addr_be, uint32_t mask_be);
void pm_ip_l2_apply_pending(void);
uint32_t pm_ip_src_for(const struct pm_metal_sock *s, uint32_t dst);
/* Egress interface plus the neighbour whose MAC carries the frame: the
 * destination when it shares a subnet with us, else the route's gateway.
 * h_hint is the socket's pinned interface, or -1 to route by address. */
int32_t pm_ip_route_out(int32_t h_hint, uint32_t src_be, uint32_t dst_be, uint32_t *hop_be);
int32_t pm_ip_l2_addr_ours(uint32_t dst_be);
void pm_ip_eth_tx(int32_t h, const uint8_t dmac[6], uint16_t ethertype, const uint8_t *body,
    uint32_t len);
void pm_ip_arp_clear(void);
int32_t pm_ip_arp_lookup(int32_t h, uint32_t addr_be, uint8_t mac[6]);
void pm_ip_arp_learn(int32_t h, uint32_t addr_be, const uint8_t mac[6]);
void pm_ip_arp_ask(int32_t h, uint32_t addr_be);
void pm_ip_arp_queue(int32_t h, uint32_t addr_be, const uint8_t *pkt, uint32_t len);
void pm_ip_arp_tick(void);
void pm_ip_arp_input(int32_t h, const uint8_t *frame, uint16_t len);
void pm_ip_arp_announce(int32_t h);

/* __wire__.c — IPv4 datagram in/out, ICMP echo, UDP demux, byte order. */
uint16_t pm_ip_csum(const uint8_t *p, uint32_t n);
/* Pseudo-header + payload, for TCP and UDP. Zero over a received packet means
 * the checksum in it is good; over an outgoing one it is the value to write. */
uint16_t pm_ip_l4_csum(const uint8_t *pkt, uint32_t total);
void pm_ip_l4_stamp(uint8_t *pkt, uint32_t total);
uint32_t pm_ip_read_be32(const uint8_t *p);
void pm_ip_write_be32(uint8_t *p, uint32_t v);
uint16_t pm_ip_read_be16(const uint8_t *p);
void pm_ip_write_be16(uint8_t *p, uint16_t v);
void pm_ip_output(const uint8_t *pkt, uint32_t len);
/* Same, but leaving by the socket's pinned interface. */
void pm_ip_output_via(int32_t h_hint, const uint8_t *pkt, uint32_t len);

/* __tcp__.c */
void pm_ip_tcp_xmit(struct pm_metal_sock *s, uint8_t flags, const uint8_t *data, uint32_t dlen);
void pm_ip_tcp_check_timeouts(void);
void pm_ip_tcp_input(uint32_t src, uint32_t dst, const uint8_t *th, uint32_t thlen);

#endif /* PYMERGETIC_METAL_NET_IP_PRIV_H */
