/*
 * Metal exp2 — lwIP options (NO_SYS raw API, DHCPv4 only).
 * Found via -I .../net/ip/cfg before external/lwip includes.
 */
#ifndef METAL_LWIPOPTS_H_
#define METAL_LWIPOPTS_H_

#define NO_SYS               1
#define LWIP_TIMERS          1
#define SYS_LIGHTWEIGHT_PROT 0

#define MEM_ALIGNMENT 8
#define MEM_SIZE      (256 * 1024)

#define MEMP_NUM_PBUF           32
#define MEMP_NUM_UDP_PCB        4
#define MEMP_NUM_RAW_PCB        4
#define MEMP_NUM_TCP_PCB        8
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_TCP_SEG        32
#define PBUF_POOL_SIZE          128
#define PBUF_POOL_BUFSIZE       1600

#define LWIP_ARP                 1
#define LWIP_ETHERNET            1
#define LWIP_IPV4                1
#define LWIP_IPV6                0
#define LWIP_ICMP                1
#define LWIP_RAW                 1
#define LWIP_UDP                 1
#define LWIP_TCP                 1
#define LWIP_DNS                 1
#define LWIP_DHCP                1
#define LWIP_ACD                 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_AUTOIP              0
#define LWIP_IGMP                0
#define LWIP_STATS               0
#define LWIP_NETCONN             0
#define LWIP_SOCKET              0
#define LWIP_NETIF_API           0

#define TCP_MSS     1460
#define TCP_WND     (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)

#define DNS_TABLE_SIZE            4
#define DNS_MAX_SERVERS           2
#define LWIP_DNS_ADDRTYPE_DEFAULT 0

#define LWIP_NETIF_HOSTNAME            1
#define LWIP_NETIF_STATUS_CALLBACK     0
#define LWIP_NETIF_LINK_CALLBACK       0
#define LWIP_NETIF_EXT_STATUS_CALLBACK 0

#define LWIP_NETIF_LOOPBACK     1
#define LWIP_HAVE_LOOPIF        0
#define LWIP_LOOPBACK_MAX_PBUFS 4

#define LWIP_DEBUG         0
#define LWIP_DBG_MIN_LEVEL LWIP_DBG_LEVEL_ALL
#define LWIP_DBG_TYPES_ON  0

#endif /* METAL_LWIPOPTS_H_ */
