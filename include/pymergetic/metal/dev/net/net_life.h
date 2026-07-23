/*
 * Background net life — lease watch + NTP + generic HTTP seed transport.
 *
 * Package *catalog* (which files, ready?) lives in guest/pkg — not here.
 * Host-only. impl: common — src/pymergetic/metal/dev/net/net_life.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_NET_LIFE_H_
#define PYMERGETIC_METAL_DEV_NET_NET_LIFE_H_

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/**
 * Start the net-life task once (idempotent). Returns 0 ok, -1 on spawn fail.
 * Safe to call after NICs are started; does not block boot.
 */
int pm_metal_net_life_start(void);

/**
 * On-demand HTTP seed for a registered guest package (by name).
 * No-op if already ready or unknown. Call from run/tab — never boot/lease.
 * Returns 0 if ready after, -1 if miss / no lease / timeout.
 */
int pm_metal_net_life_seed_ensure(const char *name);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_NET_LIFE_H_ */
