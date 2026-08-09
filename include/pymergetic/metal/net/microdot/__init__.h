#ifndef PM_METAL_NET_MICRODOT_H_
#define PM_METAL_NET_MICRODOT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Into-Py face for frozen pymergetic.metal.net.microdot (CORE muscle).
 * Soft-fail: handle 0 / -1. Does not reimplement Microdot in C.
 *
 * Public entry used by Inspect: Microdot() construction + attribute resolve.
 */

/* Resolve pymergetic.metal.net.microdot.<attr> → GC handle (0 = fail). */
uint32_t pm_metal_net_microdot_resolve(const char *attr);

/* Construct Microdot() via Py; returns GC handle (0 = unavailable). */
uint32_t pm_metal_net_microdot_new(void);

/* Drop a handle from pm_metal_net_microdot_new / resolve (no-op if 0). */
void pm_metal_net_microdot_close(uint32_t h);

/* Copy package __version__ string; returns nbytes or -1. */
int32_t pm_metal_net_microdot_version(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_NET_MICRODOT_H_ */
