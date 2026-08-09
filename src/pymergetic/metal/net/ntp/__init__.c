#include "pymergetic/metal/net/ntp/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/net/dns/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/pump/__init__.h"

#define NTP_PORT 123u
#define NTP_CLIENT_PORT 49523u
#define NTP_PACKET_LEN 48u
#define NTP_UNIX_DELTA 2208988800u

#ifndef PM_METAL_NTP_WAIT_ITERS
#define PM_METAL_NTP_WAIT_ITERS 20000u
#endif

enum { NTP_IDLE = 0, NTP_WAIT_DNS, NTP_WAIT_RX };

static uint32_t g_ah;
static uint32_t g_dns_ah;
static uint32_t g_server;
static pm_metal_net_ip_sock_h g_uh;
static uint8_t g_req[NTP_PACKET_LEN];
static uint32_t g_last_secs;
static int32_t g_status; /* 1 ok, 0 fail, -1 idle/error */
static int g_state;
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

static void ntp_fail(void)
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
    g_state = NTP_IDLE;
}

static void ntp_ok(uint32_t secs)
{
    g_last_secs = secs;
    g_status = 1;
    if (g_uh != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_uh);
        g_uh = PM_METAL_NET_IP_SOCK_INVALID;
    }
    if (g_ah != 0u && pm_metal_async_status(g_ah) != PM_METAL_ASYNC_DONE) {
        pm_metal_async_set_result_u32(g_ah, 1u);
        pm_metal_async_wake(g_ah);
    }
    g_state = NTP_IDLE;
}

static int ntp_open_send(uint32_t server_ip)
{
    memset(g_req, 0, sizeof(g_req));
    g_req[0] = 0x1bu; /* LI=0 VN=4 Mode=3 (client) */
    g_uh = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_DGRAM);
    if (g_uh == PM_METAL_NET_IP_SOCK_INVALID) {
        return -1;
    }
    if (pm_metal_net_ip_bind(g_uh, NTP_CLIENT_PORT) != 0) {
        pm_metal_net_ip_close(g_uh);
        g_uh = PM_METAL_NET_IP_SOCK_INVALID;
        return -1;
    }
    /* External hosts need ARP for next-hop; GW cache is enough if host misses. */
    if (arp_wait(server_ip, 256) != 0 && arp_wait(PM_METAL_NET_IP_DEFAULT_GW, 256) != 0) {
        pm_metal_net_ip_close(g_uh);
        g_uh = PM_METAL_NET_IP_SOCK_INVALID;
        return -1;
    }
    g_server = server_ip;
    g_attempt = 0;
    g_iters = 0;
    if (pm_metal_net_ip_sendto_ip4(g_uh, g_server, NTP_PORT, g_req, NTP_PACKET_LEN) == 0u) {
        pm_metal_net_ip_close(g_uh);
        g_uh = PM_METAL_NET_IP_SOCK_INVALID;
        return -1;
    }
    g_state = NTP_WAIT_RX;
    return 0;
}

uint32_t pm_metal_net_ntp_sync(uint32_t server_ip)
{
    if (server_ip == 0u || !pm_metal_net_ip_ready()) {
        return pm_metal_async_completed_u32(0u);
    }
    if (g_state != NTP_IDLE && g_ah != 0u) {
        return g_ah;
    }
    g_ah = pm_metal_async_park();
    if (g_ah == 0u) {
        return 0;
    }
    g_status = -1;
    g_last_secs = 0;
    if (ntp_open_send(server_ip) != 0) {
        ntp_fail();
    }
    return g_ah;
}

uint32_t pm_metal_net_ntp_sync_host(const char *host)
{
    uint32_t addr = 0;

    if (host == NULL || !pm_metal_net_ip_ready()) {
        return pm_metal_async_completed_u32(0u);
    }
    if (g_state != NTP_IDLE && g_ah != 0u) {
        return g_ah;
    }
    g_ah = pm_metal_async_park();
    if (g_ah == 0u) {
        return 0;
    }
    g_status = -1;
    g_last_secs = 0;
    /* dotted literal? */
    g_dns_ah = pm_metal_net_dns_lookup(host);
    if (g_dns_ah == 0u) {
        ntp_fail();
        return g_ah;
    }
    if (pm_metal_async_status(g_dns_ah) == PM_METAL_ASYNC_DONE) {
        if (pm_metal_async_result_u32(g_dns_ah) != 1u) {
            pm_metal_async_coro_close(g_dns_ah);
            g_dns_ah = 0;
            ntp_fail();
            return g_ah;
        }
        addr = pm_metal_net_dns_last_addr();
        pm_metal_async_coro_close(g_dns_ah);
        g_dns_ah = 0;
        if (addr == 0u || ntp_open_send(addr) != 0) {
            ntp_fail();
        }
        return g_ah;
    }
    g_state = NTP_WAIT_DNS;
    return g_ah;
}

void pm_metal_net_ntp_poll(void)
{
    uint8_t rx[NTP_PACKET_LEN + 16u];
    uint32_t rx_len = 0;
    uint32_t src_ip = 0;
    uint16_t src_port = 0;
    uint32_t ntp_secs;

    if (g_state == NTP_IDLE || g_ah == 0u) {
        return;
    }
    if (g_state == NTP_WAIT_DNS) {
        if (g_dns_ah == 0u || pm_metal_async_status(g_dns_ah) != PM_METAL_ASYNC_DONE) {
            return;
        }
        if (pm_metal_async_result_u32(g_dns_ah) != 1u) {
            pm_metal_async_coro_close(g_dns_ah);
            g_dns_ah = 0;
            ntp_fail();
            return;
        }
        {
            uint32_t addr = pm_metal_net_dns_last_addr();
            pm_metal_async_coro_close(g_dns_ah);
            g_dns_ah = 0;
            if (addr == 0u || ntp_open_send(addr) != 0) {
                ntp_fail();
            }
        }
        return;
    }
    if (g_state != NTP_WAIT_RX || g_uh == PM_METAL_NET_IP_SOCK_INVALID) {
        return;
    }
    g_iters++;
    if (pm_metal_net_ip_try_recvfrom_ip4(g_uh, &src_ip, &src_port, rx, sizeof(rx), &rx_len) == 1) {
        if (src_port == NTP_PORT && rx_len >= NTP_PACKET_LEN && (rx[0] & 0x07u) == 4u) {
            ntp_secs = ((uint32_t)rx[40] << 24) | ((uint32_t)rx[41] << 16) | ((uint32_t)rx[42] << 8) |
                       (uint32_t)rx[43];
            if (ntp_secs > NTP_UNIX_DELTA) {
                ntp_ok(ntp_secs - NTP_UNIX_DELTA);
                return;
            }
        }
    }
    if ((g_iters % 2000u) == 0u && g_attempt < 3) {
        g_attempt++;
        (void)pm_metal_net_ip_sendto_ip4(g_uh, g_server, NTP_PORT, g_req, NTP_PACKET_LEN);
    }
    if (g_iters > PM_METAL_NTP_WAIT_ITERS) {
        ntp_fail();
    }
}

int32_t pm_metal_net_ntp_status(void)
{
    return g_status;
}

uint32_t pm_metal_net_ntp_last_unix_secs(void)
{
    return g_last_secs;
}

int32_t pm_metal_net_ntp_query(uint32_t server_ip, uint32_t *unix_secs_out)
{
    uint32_t h;
    uint32_t i;

    if (unix_secs_out == NULL) {
        return -1;
    }
    h = pm_metal_net_ntp_sync(server_ip);
    if (h == 0u) {
        return -1;
    }
    for (i = 0; i < PM_METAL_NTP_WAIT_ITERS; i++) {
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
    *unix_secs_out = g_last_secs;
    pm_metal_async_coro_close(h);
    g_ah = 0;
    return 0;
}

int32_t pm_metal_net_ntp_query_host(const char *host, uint32_t *unix_secs_out)
{
    uint32_t addr = 0;

    if (host == NULL || unix_secs_out == NULL) {
        return -1;
    }
    /* Resolve via DNS sync façade, then park NTP RX (same as pre-async bring-up). */
    if (pm_metal_net_dns_resolve(host, &addr) != 0 || addr == 0u) {
        return -1;
    }
    return pm_metal_net_ntp_query(addr, unix_secs_out);
}
