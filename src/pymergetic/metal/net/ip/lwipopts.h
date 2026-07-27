/*
 * Metal EFI — lwIP options (NO_SYS raw API, DHCPv4 + stateless DHCPv6).
 * Found via -I .../metal/net before external includes.
 */
#ifndef METAL_LWIPOPTS_H_
#define METAL_LWIPOPTS_H_

#define NO_SYS               1
#define LWIP_TIMERS          1
#define SYS_LIGHTWEIGHT_PROT 0

#define MEM_ALIGNMENT 8
/* ~1 MiB class net budget with virtio TX/RX rings (see virtio_net.c). */
#define MEM_SIZE (512 * 1024)

#define MEMP_NUM_PBUF           64
#define MEMP_NUM_UDP_PCB        8
#define MEMP_NUM_TCP_PCB        16
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG        128
#define MEMP_NUM_RAW_PCB        4
#define PBUF_POOL_SIZE          256
#define PBUF_POOL_BUFSIZE       1600

#define LWIP_ARP                   1
#define LWIP_ETHERNET              1
#define LWIP_IPV4                  1
#define LWIP_IPV6                  1
#define LWIP_ICMP6                 1
#define LWIP_ICMP                  1
#define LWIP_RAW                   1
#define LWIP_UDP                   1
#define LWIP_TCP                   1
#define LWIP_DNS                   1
#define LWIP_DHCP                  1
#define LWIP_IPV6_DHCP6            1
#define LWIP_IPV6_DHCP6_STATEFUL   0
#define LWIP_DHCP6_MAX_DNS_SERVERS 2
#define LWIP_AUTOIP                0
#define LWIP_IGMP                  0
#define LWIP_STATS                 0
#define LWIP_NETCONN               0
#define LWIP_SOCKET                0
#define LWIP_NETIF_API             0

#define TCP_MSS          1460
#define TCP_WND          (32 * TCP_MSS)
#define TCP_SND_BUF      (32 * TCP_MSS)
#define TCP_SND_QUEUELEN 64

#define LWIP_NETIF_HOSTNAME            1
#define LWIP_NETIF_STATUS_CALLBACK     1
#define LWIP_NETIF_LINK_CALLBACK       1
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1

/* Capture BOOTP siaddr + file; Metal hook also keeps DHCP opts 66/67. */
#define LWIP_DHCP_BOOTP_FILE 1

/* Metal owns the "lo" netif; enable packet loopback + poll drain (NO_SYS). */
#define LWIP_NETIF_LOOPBACK     1
#define LWIP_HAVE_LOOPIF        0
#define LWIP_LOOPBACK_MAX_PBUFS 8

#define DNS_TABLE_SIZE      128 /* ~11 KiB static; was 4 — too small */
#define DNS_MAX_NAME_LENGTH 64
#define DNS_MAX_SERVERS     2
#define DNS_MAX_RETRIES     2 /* fail over to backup DNS sooner */
/* No RAND_SRC_PORT (bit 4): burns UDP PCBs under NO_SYS + small MEMP. */
#define LWIP_DNS_SECURE           (1 | 2) /* RAND_XID | NO_MULTIPLE */
#define LWIP_DNS_ADDRTYPE_DEFAULT 0       /* IPV4 only */

struct netif;
struct dhcp;
struct dhcp_msg;
struct pbuf;
void pm_metal_dhcp_parse_option(struct netif    *netif,
                                struct dhcp     *dhcp,
                                unsigned char    state,
                                struct dhcp_msg *msg,
                                unsigned char    msg_type,
                                unsigned char    option,
                                unsigned char    len,
                                struct pbuf     *pbuf,
                                unsigned short   offset);

#define LWIP_HOOK_DHCP_PARSE_OPTION(netif, dhcp, state, msg, msg_type, option, len, pbuf, offset) \
  pm_metal_dhcp_parse_option(netif, dhcp, state, msg, msg_type, option, len, pbuf, offset)

#define LWIP_DEBUG         0
#define LWIP_DBG_MIN_LEVEL LWIP_DBG_LEVEL_ALL
#define LWIP_DBG_TYPES_ON  0

#endif /* METAL_LWIPOPTS_H_ */
