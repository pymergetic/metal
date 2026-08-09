#include "pymergetic/metal/net/tls/__init__.h"

#include <string.h>

#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/fs.h"
#include "pymergetic/metal/mem.h"

static const pm_metal_net_tls_ops_t *g_ops;
static int32_t g_inited;

void pm_metal_net_tls_set_ops(const pm_metal_net_tls_ops_t *ops)
{
    g_ops = ops;
}

const pm_metal_net_tls_ops_t *pm_metal_net_tls_ops(void)
{
    return g_ops;
}

int32_t pm_metal_net_tls_init(void)
{
    if (g_inited) {
        return 0;
    }
    if (g_ops == NULL) {
        if (pm_metal_net_tls_mbedtls_register() != 0) {
            return -1;
        }
    }
    if (g_ops == NULL || g_ops->init == NULL) {
        return -1;
    }
    if (g_ops->init() != 0) {
        return -1;
    }
    g_inited = 1;
    return 0;
}

void pm_metal_net_tls_fini(void)
{
    if (g_ops != NULL && g_ops->fini != NULL) {
        g_ops->fini();
    }
    g_inited = 0;
}

pm_metal_net_tls_h pm_metal_net_tls_client_open(pm_metal_net_ip_sock_h sock, const char *hostname,
                                                uint32_t flags)
{
    if (pm_metal_net_tls_init() != 0 || g_ops->client_open == NULL) {
        return PM_METAL_NET_TLS_INVALID;
    }
    return g_ops->client_open(sock, hostname, flags);
}

pm_metal_net_tls_h pm_metal_net_tls_server_open(pm_metal_net_ip_sock_h sock)
{
    if (pm_metal_net_tls_init() != 0 || g_ops->server_open == NULL) {
        return PM_METAL_NET_TLS_INVALID;
    }
    return g_ops->server_open(sock);
}

uint32_t pm_metal_net_tls_handshake(pm_metal_net_tls_h th)
{
    if (g_ops == NULL || g_ops->handshake == NULL) {
        return pm_metal_async_completed_u32(0u);
    }
    return g_ops->handshake(th);
}

uint32_t pm_metal_net_tls_try_read(pm_metal_net_tls_h th, void *buf, uint32_t len)
{
    if (g_ops == NULL || g_ops->try_read == NULL) {
        return (uint32_t)-1;
    }
    return g_ops->try_read(th, buf, len);
}

uint32_t pm_metal_net_tls_write(pm_metal_net_tls_h th, const void *buf, uint32_t len)
{
    if (g_ops == NULL || g_ops->write == NULL) {
        return 0u;
    }
    return g_ops->write(th, buf, len);
}

void pm_metal_net_tls_close(pm_metal_net_tls_h th)
{
    if (g_ops != NULL && g_ops->close != NULL) {
        g_ops->close(th);
    }
}

int32_t pm_metal_net_tls_load_ca_pem(const uint8_t *pem, uint32_t len)
{
    if (pm_metal_net_tls_init() != 0 || g_ops->load_ca_pem == NULL) {
        return -1;
    }
    return g_ops->load_ca_pem(pem, len);
}

int32_t pm_metal_net_tls_load_ca_file(const char *path)
{
    uint32_t ah;
    uint32_t n;
    uint8_t *buf;
    int32_t rc = -1;
    uint32_t cap = 512u * 1024u;
    int i;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    if (pm_metal_net_tls_init() != 0) {
        return -1;
    }
    buf = pm_metal_mem_alloc(cap);
    if (buf == NULL) {
        return -1;
    }
    ah = pm_metal_fs_read_async((const uint8_t *)path, buf, cap);
    if (ah == 0u) {
        pm_metal_mem_free(buf);
        return -1;
    }
    for (i = 0; i < 100000; i++) {
        if (pm_metal_async_status(ah) == PM_METAL_ASYNC_DONE) {
            break;
        }
        (void)pm_metal_async_run_poll();
    }
    if (pm_metal_async_status(ah) != PM_METAL_ASYNC_DONE) {
        pm_metal_async_coro_close(ah);
        pm_metal_mem_free(buf);
        return -1;
    }
    n = pm_metal_async_result_u32(ah);
    pm_metal_async_coro_close(ah);
    if (n == 0u || n > cap) {
        pm_metal_mem_free(buf);
        return -1;
    }
    rc = pm_metal_net_tls_load_ca_pem(buf, n);
    pm_metal_mem_free(buf);
    return rc;
}

int32_t pm_metal_net_tls_set_server_cert_pem(const uint8_t *pem, uint32_t len)
{
    extern void uart_puts(const char *s);
    if (pm_metal_net_tls_init() != 0) {
        uart_puts("tls setcert init fail\n");
        return -1;
    }
    if (g_ops == NULL || g_ops->set_server_cert_pem == NULL) {
        uart_puts("tls setcert no ops\n");
        return -1;
    }
    uart_puts("tls setcert call\n");
    return g_ops->set_server_cert_pem(pem, len);
}

int32_t pm_metal_net_tls_set_server_key_pem(const uint8_t *pem, uint32_t len)
{
    if (pm_metal_net_tls_init() != 0 || g_ops->set_server_key_pem == NULL) {
        return -1;
    }
    return g_ops->set_server_key_pem(pem, len);
}

int32_t pm_metal_net_tls_set_server_chain_pem(const uint8_t *pem, uint32_t len)
{
    if (pm_metal_net_tls_init() != 0 || g_ops->set_server_chain_pem == NULL) {
        return -1;
    }
    return g_ops->set_server_chain_pem(pem, len);
}
