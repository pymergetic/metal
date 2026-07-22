/*
 * TFTP client — guest/host dual ABI (RRQ/get into a buffer).
 *
 * Async GET. After await on the returned handle:
 *   pm_metal_net_tftp_status(h) — 0 on success, else error
 *   pm_metal_net_tftp_len(h)    — bytes written to dest
 *
 * host may be a hostname, dotted IPv4, or NULL to use DHCP next-server /
 * option 66 from the default interface (when present).
 *
 * impl: common — src/pymergetic/metal/dev/net/tftp.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_TFTP_H_
#define PYMERGETIC_METAL_DEV_NET_TFTP_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_TFTP_WASI_MODULE "pymergetic.metal.net.tftp"

#if defined(__wasm__)
#define PM_METAL_NET_TFTP_IO_PTR(p) ((uint32_t)(uintptr_t)(p))
#else
#define PM_METAL_NET_TFTP_IO_PTR(p) (p)
#endif

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_NET_TFTP_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_TFTP_WASI_MODULE, name)

extern pm_metal_async_handle_t pm_metal_net_tftp_get(const char *host,
						     const char *path,
						     uint32_t dest,
						     uint32_t dest_cap)
	PM_METAL_NET_TFTP_IMPORT(pm_metal_net_tftp_get);
extern uint32_t pm_metal_net_tftp_status(pm_metal_async_handle_t h)
	PM_METAL_NET_TFTP_IMPORT(pm_metal_net_tftp_status);
extern uint32_t pm_metal_net_tftp_len(pm_metal_async_handle_t h)
	PM_METAL_NET_TFTP_IMPORT(pm_metal_net_tftp_len);
#else
pm_metal_async_handle_t pm_metal_net_tftp_get(const char *host, const char *path,
					      void *dest, uint32_t dest_cap);
uint32_t pm_metal_net_tftp_status(pm_metal_async_handle_t h);
uint32_t pm_metal_net_tftp_len(pm_metal_async_handle_t h);

int pm_metal_net_tftp_native_register(void);
void pm_metal_net_tftp_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_TFTP_H_ */
