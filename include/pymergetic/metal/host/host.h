/*
 * Local host identity (nodename) — not DNS.
 *
 * Remote name→IP stays async pm_metal_net_dns(). This API is sync kernel
 * state: who we are, default "metal", optional metal/net.conf hostname=.
 *
 * impl: common — src/pymergetic/metal/host/host.c
 */
#ifndef PYMERGETIC_METAL_HOST_HOST_H_
#define PYMERGETIC_METAL_HOST_HOST_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max nodename length including NUL (DNS label-sized). */
#define PM_METAL_HOST_NAME_MAX 64u

#define PM_METAL_HOST_WASI_MODULE "pymergetic.metal.host"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_HOST_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_HOST_WASI_MODULE, name)

/** Copy local hostname into out. Returns 0, or -1. */
extern int pm_metal_host_name_get(char *out, size_t cap)
	PM_METAL_HOST_IMPORT(pm_metal_host_name_get);
/** Set local hostname. Returns 0, or -1. */
extern int pm_metal_host_name_set(const char *name)
	PM_METAL_HOST_IMPORT(pm_metal_host_name_set);
#else
int pm_metal_host_name_get(char *out, size_t cap);
int pm_metal_host_name_set(const char *name);
/** Stable C-string of the current nodename (valid until next set). */
const char *pm_metal_host_name_cstr(void);

int pm_metal_host_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_HOST_HOST_H_ */
