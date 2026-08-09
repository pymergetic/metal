#ifndef PM_METAL_NET_NTP_H_
#define PM_METAL_NET_NTP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Async SNTP. Await handle → DONE; result_u32 1 ok / 0 fail.
 * Then status()/last_unix_secs().
 */
uint32_t pm_metal_net_ntp_sync(uint32_t server_ip);
uint32_t pm_metal_net_ntp_sync_host(const char *host);
void pm_metal_net_ntp_poll(void);
int32_t pm_metal_net_ntp_status(void); /* 1 ok, 0 fail/pending, -1 error */
uint32_t pm_metal_net_ntp_last_unix_secs(void);

/*
 * Sync façades for bring-up/smoke (pump until DONE).
 * Returns 0 ok, -1 error, -2 timeout.
 */
int32_t pm_metal_net_ntp_query(uint32_t server_ip, uint32_t *unix_secs_out);
int32_t pm_metal_net_ntp_query_host(const char *host, uint32_t *unix_secs_out);

#ifdef __cplusplus
}
#endif

#endif
