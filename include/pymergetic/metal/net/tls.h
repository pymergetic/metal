/*
 * TLS trust / config — Metal net API. Same for guests (wasi import) and host.
 * net/http uses this for peer verify; guests may query the CA path too.
 */
#ifndef PYMERGETIC_METAL_NET_TLS_H_
#define PYMERGETIC_METAL_NET_TLS_H_

#include <stddef.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_NET_TLS_WASI_MODULE "pymergetic.metal.net.tls"

#if defined(__wasm__)
#define PM_METAL_NET_TLS_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_TLS_WASI_MODULE, name)
#endif

/*
 * Resolve a file-based CA bundle path for peer verify.
 * Writes NUL-terminated path into out/cap. Returns 0 / -1.
 *
 * impl: inline → port/tls (host)
 * impl: wasi import — src/common/pymergetic/metal/net/tls.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_net_tls_ca_file(char *out, size_t cap)
	PM_METAL_NET_TLS_IMPORT(pm_metal_net_tls_ca_file);
#else
#include "pymergetic/metal/port/tls.h"

static inline int pm_metal_net_tls_ca_file(char *out, size_t cap)
{
	return pm_metal_port_tls_ca_file(out, cap);
}
#endif

#if !defined(__wasm__)
/* impl: common — src/common/pymergetic/metal/net/tls.c */
int pm_metal_net_tls_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_NET_TLS_H_ */
