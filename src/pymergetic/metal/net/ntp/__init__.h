/*
 * GENERATED
 * DO NOT HAND-EDIT THIS FILE.
 * This file is:  __init__.h
 * Edit instead:  __init__.rs
 * Source-sha: cefebe573c586d00
 * Regenerate:    metal mod sync
 * Owned by:      metal mod sync (banner = write gate)
 */

#ifndef PM_METAL_PYMERGETIC_METAL_NET_NTP_H_
#define PM_METAL_PYMERGETIC_METAL_NET_NTP_H_

#include <stddef.h> /* IWYU pragma: keep */
#include <stdint.h> /* IWYU pragma: keep */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PM_METAL_NET_NTP_OK = 0,
  PM_METAL_NET_NTP_BUSY = 1,
  PM_METAL_NET_NTP_ERR_RESOLVE = 3,
  PM_METAL_NET_NTP_ERR_SOCKET = 4,
  PM_METAL_NET_NTP_ERR_SEND = 5,
  PM_METAL_NET_NTP_ERR_REPLY = 6,
  PM_METAL_NET_NTP_ERR_TIMEOUT = 7
} pm_metal_net_ntp_status_t;

uint32_t pm_metal_net_ntp_sync(const char * host);
uint32_t pm_metal_net_ntp_status(uint32_t h);
uint64_t pm_metal_net_ntp_last_unix_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_PYMERGETIC_METAL_NET_NTP_H_ */
