#ifndef PM_METAL_NET_HTTP_H_
#define PM_METAL_NET_HTTP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Serve one fixed GET response on the established TCP socket. */
int32_t pm_metal_http_init(void);

/* Poll TCP RX; if buffer starts with GET, send HTTP/1.0 200 body "metal ok\n". */
int32_t pm_metal_http_poll(void);

/* 1 after a response has been sent. */
int32_t pm_metal_http_served(void);

#ifdef __cplusplus
}
#endif

#endif
