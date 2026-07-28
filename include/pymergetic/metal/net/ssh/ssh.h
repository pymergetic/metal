/*
 * Metal sshd control plane (Dropbear session glue).
 * Guest/host dual ABI for listen/autoload; session is host-only.
 *
 * impl: common — src/pymergetic/metal/net/ssh/ssh_server.c
 */
#ifndef PYMERGETIC_METAL_NET_SSH_H_
#define PYMERGETIC_METAL_NET_SSH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_SSH_WASI_MODULE "pymergetic.metal.net.ssh"

typedef uint32_t pm_metal_net_ssh_srv_h;

#define PM_METAL_NET_SSH_SRV_INVALID 0u

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_NET_SSH_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_NET_SSH_WASI_MODULE, name)

extern pm_metal_net_ssh_srv_h pm_metal_net_ssh_listen(uint32_t port)
  PM_METAL_NET_SSH_IMPORT(pm_metal_net_ssh_listen);
extern void pm_metal_net_ssh_close(pm_metal_net_ssh_srv_h s)
  PM_METAL_NET_SSH_IMPORT(pm_metal_net_ssh_close);
extern int32_t pm_metal_net_ssh_autoload(void) PM_METAL_NET_SSH_IMPORT(pm_metal_net_ssh_autoload);
extern int32_t pm_metal_net_ssh_status(char *buf, uint32_t buf_len)
  PM_METAL_NET_SSH_IMPORT(pm_metal_net_ssh_status);
#else
pm_metal_net_ssh_srv_h pm_metal_net_ssh_listen(uint32_t port);
void                   pm_metal_net_ssh_close(pm_metal_net_ssh_srv_h s);
int32_t                pm_metal_net_ssh_autoload(void);
/** Fill buf with one-line status; returns bytes written (excl NUL) or -1. */
int32_t pm_metal_net_ssh_status(char *buf, uint32_t buf_len);
int     pm_metal_net_ssh_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_SSH_H_ */
