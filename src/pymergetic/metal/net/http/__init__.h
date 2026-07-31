/*
 * GENERATED
 * DO NOT HAND-EDIT THIS FILE.
 * This file is:  __init__.h
 * Edit instead:  __init__.rs
 * Source-sha: e56859f483c92e01
 * Regenerate:    metal mod sync
 * Owned by:      metal mod sync (banner = write gate)
 */

#ifndef PM_METAL_PYMERGETIC_METAL_NET_HTTP_H_
#define PM_METAL_PYMERGETIC_METAL_NET_HTTP_H_

#include <stddef.h> /* IWYU pragma: keep */
#include <stdint.h> /* IWYU pragma: keep */

#ifdef __cplusplus
extern "C" {
#endif

uint32_t pm_metal_net_http_get(const char * url, void * dest, uint32_t dest_cap);
uint32_t pm_metal_net_http_status(uint32_t h);
uint32_t pm_metal_net_http_body_len(uint32_t h);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_PYMERGETIC_METAL_NET_HTTP_H_ */
