#ifndef PM_METAL_NET_NTP_H_
#define PM_METAL_NET_NTP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Query an NTP server (UDP/123). On success writes Unix epoch seconds to
 * unix_secs_out (NTP era 0 → Unix). Returns 0 ok, -1 error, -2 timeout.
 */
int32_t pm_metal_net_ntp_query(uint32_t server_ip, uint32_t *unix_secs_out);

/* Resolve host then query. */
int32_t pm_metal_net_ntp_query_host(const char *host, uint32_t *unix_secs_out);

#ifdef __cplusplus
}
#endif

#endif
