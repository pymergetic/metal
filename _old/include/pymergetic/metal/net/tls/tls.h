/*
 * TLS client/server over pm_metal_net_* (mbedTLS stream, async-friendly wire buffer).
 *
 * Host-only: HTTPS is via pm_metal_net_http_get on guests; this header is for
 * host-side TLS wiring (http.c, native_register), not a guest import surface.
 *
 * impl: common — src/pymergetic/metal/net/tls/tls.c
 */
#ifndef PYMERGETIC_METAL_NET_TLS_H_
#define PYMERGETIC_METAL_NET_TLS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

#include "pymergetic/metal/net/io_budget.h"
#include "pymergetic/metal/net/ip/ip.h"

#ifndef CONFIG_PM_METAL_TLS_WIRE_MAX
#define CONFIG_PM_METAL_TLS_WIRE_MAX CONFIG_PM_METAL_IO_WIRE_MAX
#endif
#define PM_METAL_TLS_WIRE_MAX ((uint32_t)CONFIG_PM_METAL_TLS_WIRE_MAX)

typedef struct pm_metal_net_tls_wire {
  uint8_t  buf[PM_METAL_TLS_WIRE_MAX];
  uint32_t len;
  uint32_t off;
} pm_metal_net_tls_wire_t;

typedef uint32_t pm_metal_net_tls_h;
typedef uint32_t pm_metal_net_tls_creds_h;

#define PM_METAL_TLS_INVALID       0u
#define PM_METAL_TLS_CREDS_INVALID 0u

typedef enum {
  PM_METAL_TLS_CLIENT_AUTH_NONE = 0,
  PM_METAL_TLS_CLIENT_AUTH_OPTIONAL,
  PM_METAL_TLS_CLIENT_AUTH_REQUIRED
} pm_metal_net_tls_client_auth_t;

/** Same semantics as MBEDTLS_ERR_SSL_WANT_READ / WANT_WRITE. */
#define PM_METAL_TLS_WANT_READ  (-0x6900)
#define PM_METAL_TLS_WANT_WRITE (-0x6880)

void pm_metal_net_tls_wire_reset(pm_metal_net_tls_wire_t *wire);
void pm_metal_net_tls_wire_feed(pm_metal_net_tls_wire_t *wire, const void *data, uint32_t len);

pm_metal_net_tls_h pm_metal_net_tls_open(const char *sni_host);
/**
 * Opens a server session. Credentials remain owned by the credentials handle
 * until pm_metal_net_tls_creds_close() succeeds.
 */
pm_metal_net_tls_h pm_metal_net_tls_open_server(pm_metal_net_tls_creds_h creds);
void               pm_metal_net_tls_close(pm_metal_net_tls_h h);

int32_t pm_metal_net_tls_bind(pm_metal_net_tls_h       h,
                              pm_metal_net_ip_sock_h   sock,
                              pm_metal_net_tls_wire_t *wire);
int32_t pm_metal_net_tls_bind_server(pm_metal_net_tls_h       h,
                                     pm_metal_net_ip_sock_h   sock,
                                     pm_metal_net_tls_wire_t *wire);

/**
 * Create and populate server credentials. PEM buffers need not include a NUL
 * terminator; DER buffers are passed through unchanged. client_ca may be NULL
 * only with PM_METAL_TLS_CLIENT_AUTH_NONE.
 */
pm_metal_net_tls_creds_h pm_metal_net_tls_creds_open(void);
int32_t                  pm_metal_net_tls_creds_load_buffers(pm_metal_net_tls_creds_h       h,
                                                             const void                    *cert,
                                                             uint32_t                       cert_len,
                                                             const void                    *key,
                                                             uint32_t                       key_len,
                                                             const void                    *client_ca,
                                                             uint32_t                       client_ca_len,
                                                             pm_metal_net_tls_client_auth_t client_auth);
/**
 * Load the same material from ESP/VFS paths. The client CA path may be NULL
 * only with PM_METAL_TLS_CLIENT_AUTH_NONE.
 */
int32_t pm_metal_net_tls_creds_load_paths(pm_metal_net_tls_creds_h       h,
                                          const char                    *cert_path,
                                          const char                    *key_path,
                                          const char                    *client_ca_path,
                                          pm_metal_net_tls_client_auth_t client_auth);
/** Returns 0, or -1 while a bound server session still uses h. */
int32_t pm_metal_net_tls_creds_close(pm_metal_net_tls_creds_h h);

/** 0 done, 1 need more wire I/O, -1 error */
int32_t pm_metal_net_tls_handshake_step(pm_metal_net_tls_h h);
int32_t pm_metal_net_tls_handshake_done(pm_metal_net_tls_h h);

/** Returns byte count, 0 EOF, MBEDTLS want codes, or negative error. */
int32_t pm_metal_net_tls_read(pm_metal_net_tls_h h, void *buf, uint32_t cap);
/**
 * One mbedtls_ssl_write attempt (may be partial).
 * Returns bytes written, WANT_READ/WANT_WRITE, or negative error.
 */
int32_t pm_metal_net_tls_write(pm_metal_net_tls_h h, const void *buf, uint32_t len);
/**
 * Copy the verified/presented client leaf DER after a server handshake with
 * client authentication enabled. Returns bytes copied, or 0 if unavailable
 * or cap is too small.
 */
uint32_t pm_metal_net_tls_peer_cert_der(pm_metal_net_tls_h h, void *dest, uint32_t cap);

int32_t pm_metal_net_tls_native_register(void);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_TLS_H_ */
