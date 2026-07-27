/*
 * Host-only pluggable net backend ops.
 *
 * impl: common — src/pymergetic/metal/net/ip/ip.c
 * impl: backends — src/pymergetic/metal/dev/net/{net_null,net_lwip,virtio_net}.c
 */
#ifndef PYMERGETIC_METAL_NET_IP_OPS_H_
#define PYMERGETIC_METAL_NET_IP_OPS_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/net/ip/ip.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_net_ip_ops {
  const char *name;
  int (*init)(void);
  void (*poll)(void);
  pm_metal_net_ip_sock_h (*socket)(uint32_t domain, uint32_t type);
  void (*close)(pm_metal_net_ip_sock_h h);
  pm_metal_async_handle_t (*connect)(pm_metal_net_ip_sock_h h, const char *host, uint32_t port);
  pm_metal_async_handle_t (*listen)(pm_metal_net_ip_sock_h h, uint32_t port);
  pm_metal_async_handle_t (*accept)(pm_metal_net_ip_sock_h h);
  uint32_t (*send)(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len);
  pm_metal_async_handle_t (*recv)(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len);
  pm_metal_async_handle_t (*dns)(const char *host);
  int (*bind_if)(pm_metal_net_ip_sock_h h, const char *ifname);
  /** Non-blocking recv: bytes, 0=empty, (uint32_t)-1=EOF/error. Optional. */
  uint32_t (*try_recv)(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len);
  /** Bind local port (TCP/UDP). Optional; returns 0 or -1. */
  int (*bind)(pm_metal_net_ip_sock_h h, uint32_t port);
  /** UDP sendto. Optional. */
  uint32_t (*sendto)(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len, const char *host,
                     uint32_t port);
  /** UDP recvfrom with peer. Optional. */
  uint32_t (*try_recvfrom)(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len, char *peer_host,
                           uint32_t peer_cap, uint32_t *peer_port);
} pm_metal_net_ip_ops_t;

void                      pm_metal_net_ip_set_ops(const pm_metal_net_ip_ops_t *ops);
const pm_metal_net_ip_ops_t *pm_metal_net_ip_get_ops(void);
void                      pm_metal_net_ip_poll(void);

/**
 * Probe virtio-net; on success registers DT only (open deferred until post-EBS).
 * Multiple NICs may be started — each becomes eth0, eth1, …
 * impl: common — src/pymergetic/metal/net/ip/ip_lwip.c
 */
int pm_metal_net_ip_virtio_detect(void);
/** Open virtio-net + lwIP (adds ethN). Returns 0 on success. */
int pm_metal_net_ip_virtio_start(void);
/** Probe Broadcom bge (14e4:167d et al.); on success registers DT only. */
int pm_metal_net_ip_bge_detect(void);
/** Open Broadcom bge + lwIP (adds ethN; may coexist with virtio). */
int pm_metal_net_ip_bge_start(void);
/** @deprecated use pm_metal_net_ip_virtio_detect — impl: common — net_lwip.c */
int pm_metal_net_ip_virtio_probe(void);
/** Install null ops. impl: common — src/pymergetic/metal/net/ip/ip_null.c */
void pm_metal_net_ip_null_install(void);
/**
 * Bring up loopback (`lo`, 127.0.0.1/8 + ::1). Always safe alongside NICs;
 * becomes default only when no ethN is up. Registers lwIP ops if needed.
 * impl: common — src/pymergetic/metal/net/ip/ip_lwip.c
 */
int pm_metal_net_ip_loopback_start(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_IP_OPS_H_ */
