#ifndef PM_METAL_NET_HTTP_H_
#define PM_METAL_NET_HTTP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Serve one fixed GET response on the established TCP socket. */
int32_t pm_metal_net_http_init(void);

/* Poll TCP RX; if buffer starts with GET, send HTTP/1.0 200 body "metal ok\n". */
int32_t pm_metal_net_http_poll(void);

/* 1 after a response has been sent. */
int32_t pm_metal_net_http_served(void);

/*
 * Client GET over mini-TCP. Resolves host, connects, sends HTTP/1.0 GET.
 * Copies response into buf; returns 0 on success (saw "HTTP/"), -1 error, -2 timeout.
 */
int32_t pm_metal_net_http_client_get(const char *host, uint16_t port, const char *path,
                                 uint8_t *buf, uint32_t cap, uint32_t *len_out);

#ifdef __cplusplus
}
#endif

#endif
