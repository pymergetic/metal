/*
 * Host-only pluggable net backend ops.
 *
 * impl: common — src/pymergetic/metal/dev/net/net.c
 * impl: backends — src/pymergetic/metal/dev/net/{net_null,net_lwip,virtio_net}.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_NET_OPS_H_
#define PYMERGETIC_METAL_DEV_NET_NET_OPS_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/net/net.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_net_ops {
	const char *name;
	int (*init)(void);
	void (*poll)(void);
	pm_metal_net_sock_h (*socket)(uint32_t domain, uint32_t type);
	void (*close)(pm_metal_net_sock_h h);
	pm_metal_async_handle_t (*connect)(pm_metal_net_sock_h h, const char *host,
					   uint32_t port);
	pm_metal_async_handle_t (*listen)(pm_metal_net_sock_h h, uint32_t port);
	pm_metal_async_handle_t (*accept)(pm_metal_net_sock_h h);
	uint32_t (*send)(pm_metal_net_sock_h h, const void *ptr, uint32_t len);
	pm_metal_async_handle_t (*recv)(pm_metal_net_sock_h h, void *ptr,
					uint32_t len);
	pm_metal_async_handle_t (*dns)(const char *host);
	int (*bind_if)(pm_metal_net_sock_h h, const char *ifname);
} pm_metal_net_ops_t;

void pm_metal_net_set_ops(const pm_metal_net_ops_t *ops);
const pm_metal_net_ops_t *pm_metal_net_get_ops(void);
void pm_metal_net_poll(void);

/**
 * Probe virtio-net; on success registers DT only (open deferred until post-EBS).
 * Multiple NICs may be started — each becomes eth0, eth1, …
 * impl: common — src/pymergetic/metal/dev/net/net_lwip.c
 */
int pm_metal_net_virtio_detect(void);
/** Open virtio-net + lwIP (adds ethN). Returns 0 on success. */
int pm_metal_net_virtio_start(void);
/** Probe Broadcom bge (14e4:167d et al.); on success registers DT only. */
int pm_metal_net_bge_detect(void);
/** Open Broadcom bge + lwIP (adds ethN; may coexist with virtio). */
int pm_metal_net_bge_start(void);
/** @deprecated use pm_metal_net_virtio_detect — impl: common — net_lwip.c */
int pm_metal_net_virtio_probe(void);
/** Install null ops. impl: common — src/pymergetic/metal/dev/net/net_null.c */
void pm_metal_net_null_install(void);
/**
 * Bring up loopback (`lo`, 127.0.0.1/8 + ::1). Always safe alongside NICs;
 * becomes default only when no ethN is up. Registers lwIP ops if needed.
 * impl: common — src/pymergetic/metal/dev/net/net_lwip.c
 */
int pm_metal_net_loopback_start(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_NET_OPS_H_ */
