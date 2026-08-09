#ifndef PM_METAL_NET_IP_H_
#define PM_METAL_NET_IP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* QEMU user-net defaults (guest side). */
#ifndef PM_METAL_NET_IP_DEFAULT_ADDR
#define PM_METAL_NET_IP_DEFAULT_ADDR 0x0a00020fu /* 10.0.2.15 */
#endif
#ifndef PM_METAL_NET_IP_DEFAULT_MASK
#define PM_METAL_NET_IP_DEFAULT_MASK 0xffffff00u /* /24 */
#endif
#ifndef PM_METAL_NET_IP_DEFAULT_GW
#define PM_METAL_NET_IP_DEFAULT_GW 0x0a000202u /* 10.0.2.2 */
#endif
#ifndef PM_METAL_NET_IP_DEFAULT_DNS
#define PM_METAL_NET_IP_DEFAULT_DNS 0x0a000203u /* 10.0.2.3 QEMU user DNS */
#endif
#ifndef PM_METAL_NET_IP_QEMU_SSH
#define PM_METAL_NET_IP_QEMU_SSH 0x0a000264u /* 10.0.2.100 guestfwd SSH banner */
#endif
/* Hostfwd port used by `make run` / live-ssh for guest :22. */
#ifndef PM_METAL_NET_IP_QEMU_SSH_HOSTFWD_PORT
#define PM_METAL_NET_IP_QEMU_SSH_HOSTFWD_PORT 22022u
#endif

int32_t pm_metal_net_ip_init(uint32_t addr_be, uint32_t mask_be, uint32_t gw_be);
int32_t pm_metal_net_ip_ready(void);
/* Apply a new address/mask/gw after init (e.g. DHCP lease). */
int32_t pm_metal_net_ip_set_addrs(uint32_t addr, uint32_t mask, uint32_t gw);
int32_t pm_metal_net_ip_set_dns(uint32_t dns);

uint32_t pm_metal_net_ip_addr(void);
uint32_t pm_metal_net_ip_gw(void);
uint32_t pm_metal_net_ip_mask(void);
uint32_t pm_metal_net_ip_dns(void);

/* >0 resolved, 0 pending, <0 error. */
int32_t pm_metal_net_ip_arp_resolve(uint32_t ip_host);

/* TX gratuitous ARP / announce for our address. */
int32_t pm_metal_net_ip_announce(void);

/* Poll virtio-net; handle ARP requests + ICMP echo; reap TX. */
void pm_metal_net_ip_poll(void);

/* ICMP echo request; matches replies via id/seq. Returns 0 TX ok, -2 ARP pending. */
int32_t pm_metal_net_ip_ping(uint32_t dst_ip, uint16_t id, uint16_t seq);
/* Number of matching echo replies observed since init. */
uint32_t pm_metal_net_ip_ping_replies(void);

#ifdef __cplusplus
}
#endif

#endif
