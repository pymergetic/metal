/*
 * IPv4 helpers — dotted-quad parse/format (host-order u32).
 *
 * Address layout: a.b.c.d → ((a<<24)|(b<<16)|(c<<8)|d). Unspecified is 0
 * (0.0.0.0). Pure string/int work — no DNS, no sockets.
 *
 * Single implementation, host-side only (src/pymergetic/metal/util/ip.c) —
 * mods never link a copy. On wasm32 the declarations below are wasi-style
 * imports resolved by pm_metal_util_ip_native_register().
 *
 * impl: common — src/pymergetic/metal/util/ip.c
 * impl: wasi import — src/pymergetic/metal/util/ip.c (wasm32 only)
 */
#ifndef PYMERGETIC_METAL_UTIL_IP_H_
#define PYMERGETIC_METAL_UTIL_IP_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

/* "255.255.255.255" + NUL */
#define PM_METAL_UTIL_IP4_STR_MAX 16U

#define PM_METAL_UTIL_IP_WASI_MODULE "pymergetic.metal.util.ip"

#if defined(__wasm__)
#define PM_METAL_UTIL_IP_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_UTIL_IP_WASI_MODULE, name)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse dotted-quad IPv4 into host-order u32. Returns 0, or -1.
 * impl: common / wasi import — util/ip.c
 */
#if defined(__wasm__)
extern int pm_metal_util_ip4_parse(const char *s, uint32_t *out_host)
	PM_METAL_UTIL_IP_IMPORT(pm_metal_util_ip4_parse);
#else
int pm_metal_util_ip4_parse(const char *s, uint32_t *out_host);
#endif

/**
 * Format host-order IPv4 into out (needs PM_METAL_UTIL_IP4_STR_MAX).
 * Returns snprintf-style length, or -1.
 * impl: common / wasi import — util/ip.c
 */
#if defined(__wasm__)
extern int pm_metal_util_ip4_format(uint32_t host, char *out, size_t cap)
	PM_METAL_UTIL_IP_IMPORT(pm_metal_util_ip4_format);
#else
int pm_metal_util_ip4_format(uint32_t host, char *out, size_t cap);
#endif

/** True if host-order address is 0.0.0.0. */
static inline int
pm_metal_util_ip4_is_unspecified(uint32_t host)
{
	return host == 0u;
}

/** True if `s` is already a dotted-quad IPv4 literal (no DNS). */
static inline int
pm_metal_util_ip4_is_literal(const char *s)
{
	return pm_metal_util_ip4_parse(s, NULL) == 0;
}

#if !defined(__wasm__)
/**
 * Register wasi-style imports for this module (host-only).
 * impl: common — util/ip.c
 */
int pm_metal_util_ip_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_IP_H_ */
