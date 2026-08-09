#ifndef PM_METAL_NET_HTTP_H_
#define PM_METAL_NET_HTTP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Listen on TCP :80 (cleartext smoke). */
int32_t pm_metal_net_http_init(void);
/* Listen on TCP :443 with TLS (loads smoke cert if none set). */
int32_t pm_metal_net_http_init_tls(void);
void pm_metal_net_http_shutdown(void); /* drop :80/:443 so ASGI can own them */

/* Poll accept/RX on cleartext and TLS listens. */
int32_t pm_metal_net_http_poll(void);

/* 1 after a response has been sent. */
int32_t pm_metal_net_http_served(void);

/*
 * Legacy sync GET (cleartext). Prefer pm_metal_net_http_get.
 * Returns 0 on success (saw "HTTP/"), -1 error, -2 timeout, -3 connect fail.
 */
int32_t pm_metal_net_http_client_get(const char *host, uint16_t port, const char *path,
                                     uint8_t *buf, uint32_t cap, uint32_t *len_out);

/*
 * Async HTTP(S) GET. url like http://host/path or https://host/path.
 * Await handle → DONE; then status/body accessors.
 * result_u32: 1 ok, 0 fail.
 */
uint32_t pm_metal_net_http_get(const char *url);
uint32_t pm_metal_net_http_status(void);
uint32_t pm_metal_net_http_body_len(void);
const uint8_t *pm_metal_net_http_body(void);

/* Client verify: 0 required (default), 1 none (loopback/self-signed). */
void pm_metal_net_http_set_tls_verify_none(int32_t on);

/* Advance async client state machine (called from net pump). */
void pm_metal_net_http_client_poll(void);

#ifdef __cplusplus
}
#endif

#endif
