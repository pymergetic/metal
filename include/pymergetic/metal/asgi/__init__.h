#ifndef PM_METAL_ASGI_H_
#define PM_METAL_ASGI_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* C ASGI server — Microdot/FastAPI apps sit on this later. */
int32_t pm_metal_asgi_init(uint16_t port);
int32_t pm_metal_asgi_poll(void);
int32_t pm_metal_asgi_ready(void);

#ifdef __cplusplus
}
#endif

#endif
