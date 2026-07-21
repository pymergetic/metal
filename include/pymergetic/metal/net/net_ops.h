/*
 * Host-only pluggable net backend ops.
 */
#ifndef PYMERGETIC_METAL_NET_OPS_H_
#define PYMERGETIC_METAL_NET_OPS_H_

#include <stdint.h>

#include "pymergetic/metal/async/async.h"
#include "pymergetic/metal/net/net.h"

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
} pm_metal_net_ops_t;

void pm_metal_net_set_ops(const pm_metal_net_ops_t *ops);
const pm_metal_net_ops_t *pm_metal_net_get_ops(void);
void pm_metal_net_poll(void);

/** Probe virtio-net; on success installs ops. Returns 0 if virtio ready. */
int pm_metal_net_virtio_probe(void);
/** Install null ops. */
void pm_metal_net_null_install(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_OPS_H_ */
