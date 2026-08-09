#ifndef PM_METAL_NET_DNS_H_
#define PM_METAL_NET_DNS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Async resolve (lwIP dns_gethostbyname under net.ip).
 * Await handle → DONE; result_u32 1 ok / 0 fail; then last_addr().
 */
uint32_t pm_metal_net_dns_lookup(const char *name);
uint32_t pm_metal_net_dns_last_addr(void);

/*
 * Sync façade for bring-up/smoke: parks via lookup + net pump.
 * Returns 0 on success, -1 on error/NXDOMAIN, -2 on timeout.
 */
int32_t pm_metal_net_dns_resolve(const char *name, uint32_t *addr_out);

#ifdef __cplusplus
}
#endif

#endif
