/*
 * Host IPv4 interface config (static). See docs/IO.md.
 */
#ifndef PYMERGETIC_METAL_NET_CFG_H_
#define PYMERGETIC_METAL_NET_CFG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_net_ifcfg {
	char ip[16];
	char mask[16];
	char gw[16];
	char dns[16];
	unsigned char mac[6];
	int link_up;
	const char *backend;
} pm_metal_net_ifcfg_t;

/** Fill current static config + MAC/backend. Returns 0, or -1 if net down. */
int pm_metal_net_if_get(pm_metal_net_ifcfg_t *out);

/**
 * Apply static IPv4 (dotted ASCII). dns may be NULL to leave unchanged.
 * Returns 0 on success.
 */
int pm_metal_net_if_set(const char *ip, const char *mask, const char *gw,
			const char *dns);

/** Format one-line status into buf (NUL-terminated). Returns 0. */
int pm_metal_net_if_status(char *buf, uint32_t buf_len);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_CFG_H_ */
