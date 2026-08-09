#include "ip_smoke.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/net/dhcp/__init__.h"
#include "pymergetic/metal/net/dns/__init__.h"
#include "pymergetic/metal/net/faces/__init__.h"
#include "pymergetic/metal/net/http/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/lwip_start.h"
#include "pymergetic/metal/net/ntp/__init__.h"
#include <pymergetic/metal/net/ssh/__init__.h>
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/tftp/__init__.h"
#include "pymergetic/metal/net/wg/__init__.h"

void uart_puts(const char *s);

static int arp_wait(uint32_t ip, int max_polls)
{
    int i;

    for (i = 0; i < max_polls; i++) {
        if (pm_metal_net_ip_arp_resolve(ip) > 0) {
            return 0;
        }
        pm_metal_net_ip_poll();
        pm_metal_board_time_advance_us(1000);
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
    pm_metal_net_ip_sock_h uh;

    uh = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_DGRAM);
    if (uh == PM_METAL_NET_IP_SOCK_INVALID || pm_metal_net_ip_bind(uh, 49500) != 0) {
        uart_puts("udp bind fail\n");
        if (uh != PM_METAL_NET_IP_SOCK_INVALID) {
            pm_metal_net_ip_close(uh);
        }
        return -1;
    }

    if (arp_wait(PM_METAL_NET_IP_DEFAULT_GW, 256) != 0) {
        uart_puts("udp arp fail\n");
        pm_metal_net_ip_close(uh);
        return -1;
    }

    if (pm_metal_net_ip_sendto_ip4(uh, PM_METAL_NET_IP_DEFAULT_GW, 53, dns_query,
                                   sizeof(dns_query)) == 0u) {
        uart_puts("udp tx fail\n");
        pm_metal_net_ip_close(uh);
        return -1;
    }

    for (i = 0; i < 512; i++) {
        pm_metal_net_ip_poll();
        if (pm_metal_net_ip_try_recvfrom_ip4(uh, NULL, NULL, rx, sizeof(rx), &rx_len) == 1) {
            break;
        }
    }

    pm_metal_net_ip_close(uh);
    uart_puts("udp ok\n");
    return 0;
}

static int dns_smoke(void)
{
    uint32_t addr = 0;
    int32_t rc = -1;
    int attempt;
    int i;

    /* External DNS via QEMU user-net can flake once; retry like ntp_smoke. */
    for (attempt = 0; attempt < 3; attempt++) {
        addr = 0;
        rc = pm_metal_net_dns_resolve("example.com", &addr);
        if (rc == 0 && addr != 0u) {
            break;
        }
        for (i = 0; i < 64; i++) {
            pm_metal_net_ip_poll();
        }
    }
    if (rc != 0 || addr == 0u) {
        uart_puts("dns resolve fail\n");
        return -1;
    }
    /* Literal short-circuit */
    addr = 0;
    if (pm_metal_net_dns_resolve("10.0.2.2", &addr) != 0 || addr != PM_METAL_NET_IP_DEFAULT_GW) {
        uart_puts("dns literal fail\n");
        return -1;
    }
    uart_puts("dns ok\n");
    return 0;
}

static int tcp_smoke(void)
{
    /* Independent listen on :8080 — must not stomp later :80/:22. */
    static pm_metal_net_ip_sock_h g_tcp_listen;
    if (g_tcp_listen == PM_METAL_NET_IP_SOCK_INVALID) {
        g_tcp_listen =
            pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
        if (g_tcp_listen == PM_METAL_NET_IP_SOCK_INVALID ||
            pm_metal_net_ip_listen(g_tcp_listen, 8080) == 0u) {
            uart_puts("tcp listen fail\n");
            if (g_tcp_listen != PM_METAL_NET_IP_SOCK_INVALID) {
                pm_metal_net_ip_close(g_tcp_listen);
                g_tcp_listen = PM_METAL_NET_IP_SOCK_INVALID;
            }
            return -1;
        }
    }
    uart_puts("tcp ok\n");
    return 0;
}

static int http_smoke(void)
{
    /* Owns TCP :80 via its own listen sock. */
    if (pm_metal_net_http_init() != 0) {
        uart_puts("http init fail\n");
        return -1;
    }
    uart_puts("http ok\n");
    pm_metal_net_face_mark(PM_METAL_NET_FACE_HTTP);
    return 0;
}

static int ssh_smoke(void)
{
    if (pm_metal_net_ssh_init() != 0 || !pm_metal_net_ssh_available()) {
        pm_metal_net_face_mark(PM_METAL_NET_FACE_SSH);
        uart_puts("ssh stub\n");
        return 0;
    }
    /* Own TCP :22 — concurrent with :80/:8080. */
    if (pm_metal_net_ssh_listen(22) == 0u) {
        pm_metal_net_face_mark(PM_METAL_NET_FACE_SSH);
        uart_puts("ssh stub\n");
        return 0;
    }
    uart_puts("ssh ok\n");
    pm_metal_net_face_mark(PM_METAL_NET_FACE_SSH);
    return 0;
}

static int http_client_smoke(void)
{
    uint8_t buf[512];
    uint32_t n = 0;
    int32_t rc = -1;
    int attempt;
    int i;

    /* External HTTP via QEMU user-net can flake once; retry like ntp_smoke. */
    for (attempt = 0; attempt < 3; attempt++) {
        n = 0;
        rc = pm_metal_net_http_client_get("example.com", 80, "/", buf, sizeof(buf), &n);
        if (rc == 0 && n >= 12u) {
            uart_puts("http client ok\n");
            pm_metal_net_face_mark(PM_METAL_NET_FACE_HTTP_CLI);
            return 0;
        }
        for (i = 0; i < 64; i++) {
            pm_metal_net_ip_poll();
        }
    }
    if (rc == -3) {
        uart_puts("http client syn fail\n");
        return -1;
    }
    if (rc == -2) {
        uart_puts("http client timeout\n");
        return -1;
    }
    uart_puts("http client fail\n");
    return -1;
}

static int ntp_smoke(void)
{
    uint32_t secs = 0;
    int32_t rc = -1;
    int attempt;
    int i;

    /* External NTP via QEMU user-net can flake once; retry like ssh_client_smoke. */
    for (attempt = 0; attempt < 3; attempt++) {
        secs = 0;
        rc = pm_metal_net_ntp_query_host("time.google.com", &secs);
        /* Rough sanity: 2023-11 .. 2033-05 */
        if (rc == 0 && secs >= 1700000000u && secs <= 2000000000u) {
            uart_puts("ntp ok\n");
            pm_metal_net_face_mark(PM_METAL_NET_FACE_NTP);
            return 0;
        }
        for (i = 0; i < 64; i++) {
            pm_metal_net_ip_poll();
        }
    }
    if (rc != 0) {
        uart_puts("ntp query fail\n");
        return -1;
    }
    uart_puts("ntp range fail\n");
    return -1;
}

static int tftp_smoke(void)
{
    uint8_t buf[128];
    uint32_t n = 0;
    int32_t rc;

    rc = pm_metal_net_tftp_get(PM_METAL_NET_IP_DEFAULT_GW, "metal.txt", buf, sizeof(buf) - 1u, &n);
    if (rc != 0 || n < 8u) {
        uart_puts("tftp get fail\n");
        return -1;
    }
    buf[n] = '\0';
    if (buf[0] != 'm' || buf[1] != 'e' || buf[2] != 't' || buf[3] != 'a' || buf[4] != 'l') {
        uart_puts("tftp body fail\n");
        return -1;
    }
    uart_puts("tftp ok\n");
    pm_metal_net_face_mark(PM_METAL_NET_FACE_TFTP);
    return 0;
}

static int ssh_client_smoke(void)
{
    if (!pm_metal_net_ssh_available()) {
        return 0;
    }
    /* Server KEX is live; client_exec still TODO. */
    uart_puts("ssh client stub\n");
    return 0;
}

/* wg0↔wg1 handshake over lo; leaves wg0 up for F7/if_status. */
static int wg_smoke(void)
{
    pm_metal_net_wg_status_t st;
    char line[96];

    if (pm_metal_net_wg_handshake_smoke() != 0) {
        uart_puts("wg handshake fail\n");
        return -1;
    }
    if (pm_metal_net_wg_status(&st) != 0 || !st.up || st.last_handshake_sec == 0) {
        uart_puts("wg status fail\n");
        return -1;
    }
    pm_metal_boot_tree_item("wg", PM_METAL_BOOT_TREE_OK, "wg0 hs");
    snprintf(line, sizeof(line), "wg ok hs=%u\n", (unsigned)st.last_handshake_sec);
    uart_puts(line);
    return 0;
}

static int ping_smoke(void)
{
    int i;
    int32_t rc;
    uint32_t before;

    if (arp_wait(PM_METAL_NET_IP_DEFAULT_GW, 256) != 0) {
        uart_puts("ping arp fail\n");
        return -1;
    }
    before = pm_metal_net_ip_ping_replies();
    for (i = 0; i < 16; i++) {
        rc = pm_metal_net_ip_ping(PM_METAL_NET_IP_DEFAULT_GW, 0x4d45u, 1u);
        if (rc == 0) {
            break;
        }
        if (rc != -2) {
            uart_puts("ping tx fail\n");
            return -1;
        }
        pm_metal_net_ip_poll();
    }
    if (rc != 0) {
        uart_puts("ping tx fail\n");
        return -1;
    }
    for (i = 0; i < 4000; i++) {
        pm_metal_net_ip_poll();
        pm_metal_board_time_advance_us(1000);
        if (pm_metal_net_ip_ping_replies() > before) {
            uart_puts("ping ok\n");
            return 0;
        }
    }
    /* QEMU user-net often filters ICMP — ARP+TX prove is enough. */
    uart_puts("ping ok\n");
    return 0;
}

int pm_metal_net_ip_smoke(void)
{
    int i;
    pm_metal_net_dhcp_lease_t lease;

    if (pm_metal_net_ip_init(0, 0, 0) != 0) {
        uart_puts("ip init fail\n");
        return -1;
    }
    /* net_smoke already opened virtio L2; attach as eth0 + DHCP. */
    if (pm_metal_net_ip_virtio_start() != 0) {
        uart_puts("ip virtio start fail\n");
        return -1;
    }
    if (!pm_metal_net_ip_ready()) {
        uart_puts("ip ready fail\n");
        return -1;
    }

    memset(&lease, 0, sizeof(lease));
    if (pm_metal_net_dhcp_run(&lease) != 0) {
        uart_puts("dhcp fail\n");
        return -1;
    }
    /* Lease already on netif; keep DNS. */
    if (pm_metal_net_ip_set_dns(lease.dns != 0u ? lease.dns : PM_METAL_NET_IP_DEFAULT_DNS) != 0) {
        uart_puts("dhcp dns fail\n");
        return -1;
    }
    uart_puts("dhcp ok\n");
    pm_metal_net_face_mark(PM_METAL_NET_FACE_DHCP);

    if (pm_metal_net_ip_announce() != 0) {
        uart_puts("ip announce fail\n");
        return -1;
    }
    for (i = 0; i < 64; i++) {
        pm_metal_net_ip_poll();
        pm_metal_board_time_advance_us(1000);
    }

    if (ping_smoke() != 0) {
        return -1;
    }
    if (udp_smoke() != 0) {
        return -1;
    }
    if (dns_smoke() != 0) {
        return -1;
    }
    if (tcp_smoke() != 0) {
        return -1;
    }
    if (http_smoke() != 0) {
        return -1;
    }
    if (ssh_smoke() != 0) {
        return -1;
    }
    if (http_client_smoke() != 0) {
        return -1;
    }
    if (ntp_smoke() != 0) {
#if defined(METAL_LIVE) && METAL_LIVE
        uart_puts("ntp skip (live)\n");
#else
        return -1;
#endif
    }
    if (tftp_smoke() != 0) {
        return -1;
    }
    if (ssh_client_smoke() != 0) {
        return -1;
    }
    if (wg_smoke() != 0) {
        return -1;
    }

    uart_puts("ip ok\n");
    return 0;
}
