#ifndef PM_METAL_NET_ASGI_H_
#define PM_METAL_NET_ASGI_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* C ASGI HTTP floor under net/ — Microdot/FastAPI apps sit on this later. */
int32_t pm_metal_asgi_init(uint16_t port);
/* TLS listener (default 443). Requires net/tls server cert loaded. */
int32_t pm_metal_asgi_init_tls(uint16_t port);
void pm_metal_asgi_release(void);
int32_t pm_metal_asgi_poll(void);
int32_t pm_metal_asgi_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_NET_ASGI_H_ */
