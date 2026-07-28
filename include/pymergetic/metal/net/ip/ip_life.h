/*
 * Background net life — lease watch + NTP + ASGI/SSH autoload.
 *
 * Package HTTP seed lives in guest/pkg (pkg_ensure / pkg_ensure_assets).
 * Host-only. impl: common — src/pymergetic/metal/net/ip/ip_life.c
 */
#ifndef PYMERGETIC_METAL_NET_IP_LIFE_H_
#define PYMERGETIC_METAL_NET_IP_LIFE_H_

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/**
 * Start the net-life task once (idempotent). Returns 0 ok, -1 on spawn fail.
 * Safe to call after NICs are started; does not block boot.
 */
int pm_metal_net_ip_life_start(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_IP_LIFE_H_ */
