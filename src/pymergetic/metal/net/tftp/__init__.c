#include "pymergetic/metal/net/tftp/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/pump/__init__.h"

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_net_tftp_reg_load. */
static pm_metal_reg_export_t net_tftp_exports[] = {
    PM_METAL_REG_EXPORT(get_async),
    PM_METAL_REG_EXPORT(poll),
    PM_METAL_REG_EXPORT(status),
    PM_METAL_REG_EXPORT(body_len),
    PM_METAL_REG_EXPORT(body),
    PM_METAL_REG_EXPORT(get),
};
PM_METAL_REG_REF(net_tftp, get_async, 0);
PM_METAL_REG_REF(net_tftp, poll, 1);
PM_METAL_REG_REF(net_tftp, status, 2);
PM_METAL_REG_REF(net_tftp, body_len, 3);
PM_METAL_REG_REF(net_tftp, body, 4);
PM_METAL_REG_REF(net_tftp, get, 5);
PM_METAL_REG_MOD(net_tftp, "pymergetic.metal.net.tftp")

static int32_t net_tftp_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(net_tftp_get_async, (void *)pm_metal_net_tftp_get_async);
    pm_metal_reg_export_publish(net_tftp_poll, (void *)pm_metal_net_tftp_poll);
    pm_metal_reg_export_publish(net_tftp_status, (void *)pm_metal_net_tftp_status);
    pm_metal_reg_export_publish(net_tftp_body_len, (void *)pm_metal_net_tftp_len);
    pm_metal_reg_export_publish(net_tftp_body, (void *)pm_metal_net_tftp_body);
    pm_metal_reg_export_publish(net_tftp_get, (void *)pm_metal_net_tftp_get);
    return 0;
}

#define TFTP_PORT 69u
#define TFTP_CLIENT_PORT 49569u
#define TFTP_RRQ 1u
#define TFTP_DATA 3u
#define TFTP_ACK 4u
#define TFTP_ERROR 5u

#ifndef PM_METAL_TFTP_WAIT_ITERS
#define PM_METAL_TFTP_WAIT_ITERS 20000u
#endif

static uint32_t g_ah;
static pm_metal_net_ip_sock_h g_uh;
static uint32_t g_server;
static uint8_t g_req[256];
static uint32_t g_req_len;
static uint8_t *g_buf;
static uint32_t g_cap;
static uint32_t g_len;
static int32_t g_status;
static int g_active;
static int g_attempt;
static uint32_t g_iters;

static int arp_wait(uint32_t ip, int max_polls)
{
    int i;

    for (i = 0; i < max_polls; i++) {
        if (pm_metal_net_ip_arp_resolve(ip) > 0) {
            return 0;
        }
        pm_metal_net_ip_poll();
    }
    return -1;
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void tftp_fail(void)
{
    if (g_uh != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_uh);
        g_uh = PM_METAL_NET_IP_SOCK_INVALID;
    }
    if (g_ah != 0u && pm_metal_async_status(g_ah) != PM_METAL_ASYNC_DONE) {
        pm_metal_async_set_result_u32(g_ah, 0u);
        pm_metal_async_wake(g_ah);
    }
    g_status = 0;
    g_active = 0;
}

static void tftp_ok(uint32_t n)
{
    g_len = n;
    g_status = 1;
    if (g_uh != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_uh);
        g_uh = PM_METAL_NET_IP_SOCK_INVALID;
    }
    if (g_ah != 0u && pm_metal_async_status(g_ah) != PM_METAL_ASYNC_DONE) {
        pm_metal_async_set_result_u32(g_ah, 1u);
        pm_metal_async_wake(g_ah);
    }
    g_active = 0;
}

uint32_t pm_metal_net_tftp_get_async(uint32_t server_ip, const char *filename, uint8_t *buf,
                                     uint32_t cap)
{
    size_t nlen = 0;
    size_t o;

    if (server_ip == 0u || filename == NULL || buf == NULL || cap == 0u || !pm_metal_net_ip_ready()) {
        return pm_metal_async_completed_u32(0u);
    }
    while (filename[nlen] != '\0') {
        nlen++;
    }
    if (nlen == 0u || nlen > 128u) {
        return pm_metal_async_completed_u32(0u);
    }
    if (g_active && g_ah != 0u) {
        return g_ah;
    }
    g_ah = pm_metal_async_park();
    if (g_ah == 0u) {
        return 0;
    }
    g_buf = buf;
    g_cap = cap;
    g_len = 0;
    g_status = -1;
    put_u16(g_req, TFTP_RRQ);
    o = 2;
    memcpy(g_req + o, filename, nlen);
    o += nlen;
    g_req[o++] = 0;
    memcpy(g_req + o, "octet", 5);
    o += 5;
    g_req[o++] = 0;
    g_req_len = (uint32_t)o;
    g_uh = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_DGRAM);
    if (g_uh == PM_METAL_NET_IP_SOCK_INVALID || pm_metal_net_ip_bind(g_uh, TFTP_CLIENT_PORT) != 0) {
        if (g_uh != PM_METAL_NET_IP_SOCK_INVALID) {
            pm_metal_net_ip_close(g_uh);
            g_uh = PM_METAL_NET_IP_SOCK_INVALID;
        }
        tftp_fail();
        return g_ah;
    }
    if (arp_wait(server_ip, 256) != 0 && arp_wait(PM_METAL_NET_IP_DEFAULT_GW, 256) != 0) {
        tftp_fail();
        return g_ah;
    }
    g_server = server_ip;
    g_attempt = 0;
    g_iters = 0;
    g_active = 1;
    if (pm_metal_net_ip_sendto_ip4(g_uh, g_server, TFTP_PORT, g_req, g_req_len) == 0u) {
        tftp_fail();
    }
    return g_ah;
}

void pm_metal_net_tftp_poll(void)
{
    uint8_t rx[600];
    uint8_t ack[4];
    uint32_t rx_len = 0;
    uint32_t src_ip = 0;
    uint16_t src_port = 0;

    if (!g_active || g_uh == PM_METAL_NET_IP_SOCK_INVALID) {
        return;
    }
    g_iters++;
    if (pm_metal_net_ip_try_recvfrom_ip4(g_uh, &src_ip, &src_port, rx, sizeof(rx), &rx_len) == 1) {
        if (src_ip == g_server && rx_len >= 4u) {
            if (get_u16(rx) == TFTP_ERROR) {
                tftp_fail();
                return;
            }
            if (get_u16(rx) == TFTP_DATA && get_u16(rx + 2) == 1u) {
                uint32_t payload = rx_len - 4u;
                if (payload > g_cap) {
                    payload = g_cap;
                }
                memcpy(g_buf, rx + 4, payload);
                put_u16(ack, TFTP_ACK);
                put_u16(ack + 2, 1u);
                (void)pm_metal_net_ip_sendto_ip4(g_uh, g_server, src_port, ack, 4u);
                tftp_ok(payload);
                return;
            }
        }
    }
    if ((g_iters % 2000u) == 0u && g_attempt < 3) {
        g_attempt++;
        (void)pm_metal_net_ip_sendto_ip4(g_uh, g_server, TFTP_PORT, g_req, g_req_len);
    }
    if (g_iters > PM_METAL_TFTP_WAIT_ITERS) {
        tftp_fail();
    }
}

int32_t pm_metal_net_tftp_status(void)
{
    return g_status;
}

uint32_t pm_metal_net_tftp_len(void)
{
    return g_len;
}

const uint8_t *pm_metal_net_tftp_body(void)
{
    return g_buf;
}

int32_t pm_metal_net_tftp_get(uint32_t server_ip, const char *filename, uint8_t *buf, uint32_t cap,
                              uint32_t *len_out)
{
    uint32_t h;
    uint32_t i;

    if (len_out == NULL) {
        return -1;
    }
    *len_out = 0;
    h = pm_metal_net_tftp_get_async(server_ip, filename, buf, cap);
    if (h == 0u) {
        return -1;
    }
    for (i = 0; i < PM_METAL_TFTP_WAIT_ITERS; i++) {
        pm_metal_net_pump_once();
        pm_metal_board_time_advance_us(1000);
        (void)pm_metal_async_run_poll();
        if (pm_metal_async_status(h) == PM_METAL_ASYNC_DONE) {
            break;
        }
    }
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE || pm_metal_async_result_u32(h) != 1u) {
        pm_metal_async_coro_close(h);
        g_ah = 0;
        return -2;
    }
    *len_out = g_len;
    pm_metal_async_coro_close(h);
    g_ah = 0;
    return 0;
}
