#include "ip_smoke.h"

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ip_internal.h"
#include "pymergetic/metal/net/tcp.h"
#include "pymergetic/metal/net/udp.h"

void uart_puts(const char *s);

static int arp_wait(uint32_t ip, int max_polls)
{
    int i;

    for (i = 0; i < max_polls; i++) {
        if (pm_metal_ip_arp_resolve(ip) > 0) {
            return 0;
        }
        pm_metal_ip_poll();
    }
    return -1;
}

static int udp_smoke(void)
{
    static const uint8_t dns_query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x61, 0x07, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x03, 0x63,
        0x6f, 0x6d, 0x00, 0x00, 0x01, 0x00, 0x01,
    };
    uint8_t rx[256];
    uint32_t rx_len;
    int i;
    int32_t rc;

    if (pm_metal_udp_bind(49500) != 0) {
        uart_puts("udp bind fail\n");
        return -1;
    }

    if (arp_wait(PM_METAL_IP_DEFAULT_GW, 256) != 0) {
        uart_puts("udp arp fail\n");
        return -1;
    }

    for (i = 0; i < 8; i++) {
        rc = pm_metal_udp_sendto(PM_METAL_IP_DEFAULT_GW, 53, dns_query, sizeof(dns_query));
        if (rc == 0) {
            break;
        }
        if (rc != -2) {
            uart_puts("udp tx fail\n");
            return -1;
        }
        pm_metal_ip_poll();
    }
    if (rc != 0) {
        uart_puts("udp tx fail\n");
        return -1;
    }

    for (i = 0; i < 512; i++) {
        pm_metal_ip_poll();
        if (pm_metal_udp_recv(NULL, NULL, rx, sizeof(rx), &rx_len) == 1) {
            break;
        }
    }

    uart_puts("udp ok\n");
    return 0;
}

static int tcp_smoke(void)
{
    if (pm_metal_tcp_listen(8080) != 0) {
        uart_puts("tcp listen fail\n");
        return -1;
    }
    if (pm_metal_tcp_smoke_syn_ack() != 0) {
        uart_puts("tcp syn fail\n");
        return -1;
    }
    uart_puts("tcp ok\n");
    return 0;
}

static int http_smoke(void)
{
    static const char get_req[] = "GET / HTTP/1.0\r\nHost: metal\r\n\r\n";
    int i;

    if (pm_metal_tcp_listen(80) != 0) {
        uart_puts("http listen fail\n");
        return -1;
    }
    if (pm_metal_tcp_smoke_syn_ack() != 0) {
        uart_puts("http syn fail\n");
        return -1;
    }
    if (pm_metal_http_init() != 0) {
        uart_puts("http init fail\n");
        return -1;
    }
    if (pm_metal_tcp_smoke_inject_payload(get_req, (uint32_t)(sizeof(get_req) - 1u)) != 0) {
        uart_puts("http inject fail\n");
        return -1;
    }
    for (i = 0; i < 8; i++) {
        if (pm_metal_http_poll() < 0) {
            uart_puts("http poll fail\n");
            return -1;
        }
        if (pm_metal_http_served()) {
            break;
        }
        pm_metal_ip_poll();
    }
    if (!pm_metal_http_served()) {
        uart_puts("http serve fail\n");
        return -1;
    }
    uart_puts("http ok\n");
    return 0;
}

int pm_metal_ip_smoke(void)
{
    int i;

    if (pm_metal_ip_init(PM_METAL_IP_DEFAULT_ADDR, PM_METAL_IP_DEFAULT_MASK,
                         PM_METAL_IP_DEFAULT_GW) != 0) {
        uart_puts("ip init fail\n");
        return -1;
    }
    if (!pm_metal_ip_ready()) {
        uart_puts("ip ready fail\n");
        return -1;
    }
    if (pm_metal_ip_announce() != 0) {
        uart_puts("ip announce fail\n");
        return -1;
    }
    for (i = 0; i < 64; i++) {
        pm_metal_ip_poll();
    }

    if (udp_smoke() != 0) {
        return -1;
    }
    if (tcp_smoke() != 0) {
        return -1;
    }
    if (http_smoke() != 0) {
        return -1;
    }

    uart_puts("ip ok\n");
    return 0;
}
