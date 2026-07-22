/*
 * Host network interface config (multi-if eth0..ethN). See docs/IO.md.
 *
 * Host-only (shell / boot): guests use pm_metal_net_* async I/O, not ifconfig.
 *
 * impl: common — src/pymergetic/metal/dev/net/net_lwip.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_NET_CFG_H_
#define PYMERGETIC_METAL_DEV_NET_NET_CFG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

#define PM_METAL_NET_IFNAME_MAX     8
#define PM_METAL_NET_MAX_IFS        4
#define PM_METAL_NET_TFTP_HOST_MAX  64
#define PM_METAL_NET_BOOT_FILE_MAX  128

#define PM_METAL_NET_DHCP6_OFF        0
#define PM_METAL_NET_DHCP6_STATELESS  1
#define PM_METAL_NET_DHCP6_STATEFUL   2

typedef struct pm_metal_net_ifcfg {
	char name[PM_METAL_NET_IFNAME_MAX];
	char ip[16];
	char mask[16];
	char gw[16];
	char dns[16];
	char tftp[PM_METAL_NET_TFTP_HOST_MAX];
	char boot_file[PM_METAL_NET_BOOT_FILE_MAX];
	unsigned char mac[6];
	int link_up;
	const char *backend;
} pm_metal_net_ifcfg_t;

/** Number of active interfaces (`lo` + eth0 .. ethN-1). */
unsigned pm_metal_net_if_count(void);

/** Fill config for interface index [0, count). Returns 0, or -1. */
int pm_metal_net_if_get_index(unsigned index, pm_metal_net_ifcfg_t *out);

/** Fill config for named interface ("eth0"). Returns 0, or -1. */
int pm_metal_net_if_get_named(const char *name, pm_metal_net_ifcfg_t *out);

/** Default interface (eth0): same as get_index(0) when present. */
int pm_metal_net_if_get(pm_metal_net_ifcfg_t *out);

/**
 * Apply static IPv4 on named interface. name NULL → default (eth0).
 * dns may be NULL to leave unchanged. Returns 0 on success.
 */
int pm_metal_net_if_set_named(const char *name, const char *ip,
			      const char *mask, const char *gw,
			      const char *dns);

/** Apply static IPv4 on default interface (eth0). */
int pm_metal_net_if_set(const char *ip, const char *mask, const char *gw,
			const char *dns);

/** Switch named interface to DHCPv4 + stateless DHCPv6. name NULL → eth0. */
int pm_metal_net_if_set_dhcp_named(const char *name);

/** Set DHCPv6 mode on named interface (off/stateless/stateful). name NULL → eth0. */
int pm_metal_net_if_set_dhcp6_named(const char *name, int mode);

/** Switch default interface (eth0) to DHCP. */
int pm_metal_net_if_set_dhcp(void);

/** Format all interfaces (one line each, newline-separated). Returns 0. */
int pm_metal_net_if_status(char *buf, uint32_t buf_len);

/** Format one interface line. name NULL → default. Returns 0. */
int pm_metal_net_if_status_named(const char *name, char *buf, uint32_t buf_len);

/**
 * DHCP boot/TFTP info for named iface (NULL → default).
 * Fills tftp host (opt 66 or siaddr) and boot file (opt 67 or BOOTP file).
 * Empty strings if unknown. Returns 0, or -1.
 */
int pm_metal_net_if_boot_get(const char *name, char *tftp_host,
			     uint32_t tftp_cap, char *boot_file,
			     uint32_t boot_cap);

/** Refresh lwIP netif hostname + renew DHCP after nodename change. */
void pm_metal_net_on_hostname_changed(void);

/**
 * Sync IPv4 resolve: literal, localhost/nodename, /etc/hosts, or DNS cache.
 * out_host is host-order. Returns 0, or -1 (need async DNS / unknown).
 */
int pm_metal_net_resolve_ip4(const char *host, uint32_t *out_host);

/**
 * After await on pm_metal_net_dns with success (result 1): last address as
 * ASCII (IPv4 or IPv6). Returns 0, or -1 if none.
 */
int pm_metal_net_dns_last_ntoa(char *out, uint32_t out_cap);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_NET_CFG_H_ */
