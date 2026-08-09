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
 * Bridge count is intentional subset of the Py public surface (~20 symbols);
 * full decorator/route API stays on the Py muscle.
 */

/* Resolve pymergetic.metal.net.microdot.<attr> → GC handle (0 = fail). */
uint32_t pm_metal_net_microdot_resolve(const char *attr);

/* Construct Microdot() via Py; returns GC handle (0 = unavailable). */
uint32_t pm_metal_net_microdot_new(void);

/* Drop a handle from new / resolve / getattr / call* (no-op if 0). */
void pm_metal_net_microdot_close(uint32_t h);

/* Copy package __version__ string; returns nbytes or -1. */
int32_t pm_metal_net_microdot_version(char *buf, size_t buf_len);

/* Package-level resolve shortcuts (same as resolve("…")). */
uint32_t pm_metal_net_microdot_request(void);
uint32_t pm_metal_net_microdot_response(void);
uint32_t pm_metal_net_microdot_abort(void);
uint32_t pm_metal_net_microdot_redirect(void);
uint32_t pm_metal_net_microdot_send_file(void);
uint32_t pm_metal_net_microdot_url_pattern(void);
uint32_t pm_metal_net_microdot_async_bytes_io(void);
uint32_t pm_metal_net_microdot_iscoroutine(void);

/* Instance/class ops on a rooted handle. */
uint32_t pm_metal_net_microdot_getattr(uint32_t h, const char *attr);
uint32_t pm_metal_net_microdot_call0(uint32_t h);
uint32_t pm_metal_net_microdot_call_method0(uint32_t h, const char *method);
uint32_t pm_metal_net_microdot_call_method1(uint32_t h, const char *method,
                                           const char *arg);

/* App-handle conveniences (getattr of common Microdot methods). */
uint32_t pm_metal_net_microdot_route(uint32_t app_h);
uint32_t pm_metal_net_microdot_run(uint32_t app_h);
uint32_t pm_metal_net_microdot_get(uint32_t app_h);
uint32_t pm_metal_net_microdot_post(uint32_t app_h);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_NET_MICRODOT_H_ */
