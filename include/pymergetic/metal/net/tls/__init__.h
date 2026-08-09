#ifndef PM_METAL_NET_TLS_H_
#define PM_METAL_NET_TLS_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/net/ip/sock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_net_tls_h;
#define PM_METAL_NET_TLS_INVALID 0u

/* Client verify policy (default: required). */
#define PM_METAL_NET_TLS_VERIFY_REQUIRED 0u
#define PM_METAL_NET_TLS_VERIFY_NONE 1u

typedef struct pm_metal_net_tls_ops {
    uint32_t version;
    int32_t (*init)(void);
    void (*fini)(void);
    pm_metal_net_tls_h (*client_open)(pm_metal_net_ip_sock_h sock, const char *hostname,
                                      uint32_t flags);
    pm_metal_net_tls_h (*server_open)(pm_metal_net_ip_sock_h sock);
    /* Async: park handle; 1 ok / 0 fail in result_u32 after DONE. */
    uint32_t (*handshake)(pm_metal_net_tls_h th);
    /* Nonblocking: >0 bytes, 0 would-block, (uint32_t)-1 error/EOF. */
    uint32_t (*try_read)(pm_metal_net_tls_h th, void *buf, uint32_t len);
    uint32_t (*write)(pm_metal_net_tls_h th, const void *buf, uint32_t len);
    void (*close)(pm_metal_net_tls_h th);
    int32_t (*load_ca_pem)(const uint8_t *pem, uint32_t len);
    int32_t (*set_server_cert_pem)(const uint8_t *pem, uint32_t len);
    int32_t (*set_server_key_pem)(const uint8_t *pem, uint32_t len);
    int32_t (*set_server_chain_pem)(const uint8_t *pem, uint32_t len);
} pm_metal_net_tls_ops_t;

#define PM_METAL_NET_TLS_OPS_VERSION 1u

void pm_metal_net_tls_set_ops(const pm_metal_net_tls_ops_t *ops);
const pm_metal_net_tls_ops_t *pm_metal_net_tls_ops(void);

/* Register default mbedtls backend (idempotent). */
int32_t pm_metal_net_tls_mbedtls_register(void);

int32_t pm_metal_net_tls_init(void);
void pm_metal_net_tls_fini(void);

pm_metal_net_tls_h pm_metal_net_tls_client_open(pm_metal_net_ip_sock_h sock, const char *hostname,
                                                uint32_t flags);
pm_metal_net_tls_h pm_metal_net_tls_server_open(pm_metal_net_ip_sock_h sock);
uint32_t pm_metal_net_tls_handshake(pm_metal_net_tls_h th);
uint32_t pm_metal_net_tls_try_read(pm_metal_net_tls_h th, void *buf, uint32_t len);
uint32_t pm_metal_net_tls_write(pm_metal_net_tls_h th, const void *buf, uint32_t len);
void pm_metal_net_tls_close(pm_metal_net_tls_h th);

int32_t pm_metal_net_tls_load_ca_pem(const uint8_t *pem, uint32_t len);
/* Load Mozilla-style PEM from VFS path (e.g. /etc/ssl/cert.pem). */
int32_t pm_metal_net_tls_load_ca_file(const char *path);
int32_t pm_metal_net_tls_set_server_cert_pem(const uint8_t *pem, uint32_t len);
int32_t pm_metal_net_tls_set_server_key_pem(const uint8_t *pem, uint32_t len);
int32_t pm_metal_net_tls_set_server_chain_pem(const uint8_t *pem, uint32_t len);

/* Install embedded smoke server cert+key (dev/QEMU). */
int32_t pm_metal_net_tls_load_smoke_server(void);

/* Pump: advance pending TLS handshakes (WANT_READ/WRITE). */
void pm_metal_net_tls_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_NET_TLS_H_ */
