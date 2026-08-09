#ifndef PM_METAL_SERVICES_H_
#define PM_METAL_SERVICES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start product listeners: ASGI :80 + :443 (TLS), SSH :22.
 * Returns 0 if at least ASGI :80 started; does not take the poll loop.
 */
int32_t pm_metal_net_services_start(void);

#ifdef __cplusplus
}
#endif

#endif
