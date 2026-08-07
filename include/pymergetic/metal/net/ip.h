#ifndef PM_METAL_NET_IP_H_
#define PM_METAL_NET_IP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* QEMU user-net defaults (guest side). */
#ifndef PM_METAL_IP_DEFAULT_ADDR
#define PM_METAL_IP_DEFAULT_ADDR 0x0a00020fu /* 10.0.2.15 */
#endif
#ifndef PM_METAL_IP_DEFAULT_MASK
#define PM_METAL_IP_DEFAULT_MASK 0xffffff00u /* /24 */
#endif
#ifndef PM_METAL_IP_DEFAULT_GW
#define PM_METAL_IP_DEFAULT_GW 0x0a000202u /* 10.0.2.2 */
#endif
#ifndef PM_METAL_IP_DEFAULT_DNS
#define PM_METAL_IP_DEFAULT_DNS 0x0a000203u /* 10.0.2.3 QEMU user DNS */
#endif

int32_t pm_metal_ip_init(uint32_t addr_be, uint32_t mask_be, uint32_t gw_be);
int32_t pm_metal_ip_ready(void);

uint32_t pm_metal_ip_addr(void);
uint32_t pm_metal_ip_gw(void);

/* TX gratuitous ARP / announce for our address. */
int32_t pm_metal_ip_announce(void);

/* Poll virtio-net; handle ARP requests + ICMP echo; reap TX. */
void pm_metal_ip_poll(void);

/* ICMP echo request; matches replies via id/seq. Returns 0 TX ok, -2 ARP pending. */
int32_t pm_metal_ip_ping(uint32_t dst_ip, uint16_t id, uint16_t seq);
/* Number of matching echo replies observed since init. */
uint32_t pm_metal_ip_ping_replies(void);

#ifdef __cplusplus
}
#endif

#endif
