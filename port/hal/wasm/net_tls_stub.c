/*
 * Browser net.tls — same C ABI; sock-TLS unavailable (HTTPS uses net.http/js.fetch).
 */
#include "pymergetic/metal/net/tls/__init__.h"
#include "pymergetic/metal/async/handle.h"

#include <stddef.h>

static const pm_metal_net_tls_ops_t *g_ops;

void pm_metal_net_tls_set_ops(const pm_metal_net_tls_ops_t *ops)
{
    g_ops = ops;
}

const pm_metal_net_tls_ops_t *pm_metal_net_tls_ops(void)
{
    return g_ops;
}

int32_t pm_metal_net_tls_mbedtls_register(void)
{
    return -1;
}

int32_t pm_metal_net_tls_init(void)
{
    return -1;
}

void pm_metal_net_tls_fini(void) {}

pm_metal_net_tls_h pm_metal_net_tls_client_open(pm_metal_net_ip_sock_h sock, const char *hostname,
                                                uint32_t flags)
{
    (void)sock;
    (void)hostname;
    (void)flags;
    return PM_METAL_NET_TLS_INVALID;
}

pm_metal_net_tls_h pm_metal_net_tls_server_open(pm_metal_net_ip_sock_h sock)
{
    (void)sock;
    return PM_METAL_NET_TLS_INVALID;
}

uint32_t pm_metal_net_tls_handshake(pm_metal_net_tls_h th)
{
    (void)th;
    return pm_metal_async_completed_u32(0u);
}

uint32_t pm_metal_net_tls_try_read(pm_metal_net_tls_h th, void *buf, uint32_t len)
{
    (void)th;
    (void)buf;
    (void)len;
    return (uint32_t)-1;
}

uint32_t pm_metal_net_tls_write(pm_metal_net_tls_h th, const void *buf, uint32_t len)
{
    (void)th;
    (void)buf;
    (void)len;
    return (uint32_t)-1;
}

void pm_metal_net_tls_close(pm_metal_net_tls_h th)
{
    (void)th;
}

int32_t pm_metal_net_tls_load_ca_pem(const uint8_t *pem, uint32_t len)
{
    (void)pem;
    (void)len;
    return -1;
}

int32_t pm_metal_net_tls_load_ca_file(const char *path)
{
    (void)path;
    return -1;
}

int32_t pm_metal_net_tls_set_server_cert_pem(const uint8_t *pem, uint32_t len)
{
    (void)pem;
    (void)len;
    return -1;
}

int32_t pm_metal_net_tls_set_server_key_pem(const uint8_t *pem, uint32_t len)
{
    (void)pem;
    (void)len;
    return -1;
}

int32_t pm_metal_net_tls_set_server_chain_pem(const uint8_t *pem, uint32_t len)
{
    (void)pem;
    (void)len;
    return -1;
}

int32_t pm_metal_net_tls_load_smoke_server(void)
{
    return -1;
}

void pm_metal_net_tls_poll(void) {}
