#include "ip_smoke.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/dhcp.h"
#include "pymergetic/metal/net/dns.h"
#include "pymergetic/metal/net/faces.h"
#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ip_internal.h"
#include "pymergetic/metal/net/ntp.h"
#include "pymergetic/metal/net/ssh.h"
#include "pymergetic/metal/net/tcp.h"
#include "pymergetic/metal/net/tftp.h"
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

static int dns_smoke(void)
{
    uint32_t addr = 0;
    int32_t rc;

    rc = pm_metal_dns_resolve("example.com", &addr);
    if (rc != 0 || addr == 0u) {
        uart_puts("dns resolve fail\n");
        return -1;
    }
    /* Literal short-circuit */
    addr = 0;
    if (pm_metal_dns_resolve("10.0.2.2", &addr) != 0 || addr != PM_METAL_IP_DEFAULT_GW) {
        uart_puts("dns literal fail\n");
        return -1;
    }
    uart_puts("dns ok\n");
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
    pm_metal_net_face_mark(PM_METAL_NET_FACE_HTTP);
    return 0;
}

static int ssh_smoke(void)
{
    if (pm_metal_tcp_listen(22) != 0) {
        uart_puts("ssh listen fail\n");
        return -1;
    }
    if (pm_metal_tcp_smoke_syn_ack() != 0) {
        uart_puts("ssh syn fail\n");
        return -1;
    }
    if (pm_metal_ssh_banner_send() != 0) {
        uart_puts("ssh banner fail\n");
        return -1;
    }
    if (!pm_metal_ssh_banner_sent()) {
        uart_puts("ssh sent fail\n");
        return -1;
    }
    uart_puts("ssh ok\n");
    pm_metal_net_face_mark(PM_METAL_NET_FACE_SSH);
    return 0;
}

static int http_client_smoke(void)
{
    uint8_t buf[512];
    uint32_t n = 0;
    int32_t rc;

    rc = pm_metal_http_client_get("example.com", 80, "/", buf, sizeof(buf), &n);
    if (rc == -3) {
        uart_puts("http client syn fail\n");
        return -1;
    }
    if (rc == -2) {
        uart_puts("http client timeout\n");
        return -1;
    }
    if (rc != 0 || n < 12u) {
        uart_puts("http client fail\n");
        return -1;
    }
    uart_puts("http client ok\n");
    pm_metal_net_face_mark(PM_METAL_NET_FACE_HTTP_CLI);
    return 0;
}

static int ntp_smoke(void)
{
    uint32_t secs = 0;
    int32_t rc;

    rc = pm_metal_ntp_query_host("time.google.com", &secs);
    if (rc != 0) {
        uart_puts("ntp query fail\n");
        return -1;
    }
    /* Rough sanity: 2023-11 .. 2033-05 */
    if (secs < 1700000000u || secs > 2000000000u) {
        uart_puts("ntp range fail\n");
        return -1;
    }
    uart_puts("ntp ok\n");
    pm_metal_net_face_mark(PM_METAL_NET_FACE_NTP);
    return 0;
}

static int tftp_smoke(void)
{
    uint8_t buf[128];
    uint32_t n = 0;
    int32_t rc;

    rc = pm_metal_tftp_get(PM_METAL_IP_DEFAULT_GW, "metal.txt", buf, sizeof(buf) - 1u, &n);
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
    uint8_t buf[128];
    uint32_t n = 0;
    int32_t rc;
    int attempt;

    /* QEMU user-net guestfwd at 10.0.2.100:22 (see qemu-ssh-banner.sh). */
    for (attempt = 0; attempt < 3; attempt++) {
        n = 0;
        rc = pm_metal_ssh_client_ident_ip(PM_METAL_IP_QEMU_SSH, 22, buf, sizeof(buf), &n);
        if (rc == 0 && n >= 7u) {
            uart_puts("ssh client ok\n");
            pm_metal_net_face_mark(PM_METAL_NET_FACE_SSH_CLI);
            /* Listen PCB must survive the outbound client PCB. */
            if (!pm_metal_tcp_passive_listening()) {
                uart_puts("tcp dual fail\n");
                return -1;
            }
            uart_puts("tcp dual ok\n");
            return 0;
        }
    }
    if (rc == -3) {
        uart_puts("ssh client syn fail\n");
        return -1;
    }
    if (rc == -2) {
        uart_puts("ssh client timeout\n");
        return -1;
    }
    uart_puts("ssh client fail\n");
    return -1;
}

static int ping_smoke(void)
{
    int i;
    int32_t rc;
    uint32_t before;

    if (arp_wait(PM_METAL_IP_DEFAULT_GW, 256) != 0) {
        uart_puts("ping arp fail\n");
        return -1;
    }
    before = pm_metal_ip_ping_replies();
    for (i = 0; i < 16; i++) {
        rc = pm_metal_ip_ping(PM_METAL_IP_DEFAULT_GW, 0x4d45u, 1u);
        if (rc == 0) {
            break;
        }
        if (rc != -2) {
            uart_puts("ping tx fail\n");
            return -1;
        }
        pm_metal_ip_poll();
    }
    if (rc != 0) {
        uart_puts("ping tx fail\n");
        return -1;
    }
    for (i = 0; i < 4000; i++) {
        pm_metal_ip_poll();
        if (pm_metal_ip_ping_replies() > before) {
            uart_puts("ping ok\n");
            return 0;
        }
    }
    uart_puts("ping reply fail\n");
    return -1;
}

int pm_metal_ip_smoke(void)
{
    int i;
    pm_metal_dhcp_lease_t lease;

    if (pm_metal_ip_init(0, 0, 0) != 0) {
        uart_puts("ip init fail\n");
        return -1;
    }
    if (!pm_metal_ip_ready()) {
        uart_puts("ip ready fail\n");
        return -1;
    }

    memset(&lease, 0, sizeof(lease));
    if (pm_metal_dhcp_run(&lease) != 0) {
        uart_puts("dhcp fail\n");
        return -1;
    }
    if (pm_metal_ip_set_addrs(lease.yiaddr, lease.mask, lease.gw) != 0) {
        uart_puts("dhcp apply fail\n");
        return -1;
    }
    if (pm_metal_ip_set_dns(lease.dns != 0u ? lease.dns : PM_METAL_IP_DEFAULT_DNS) != 0) {
        uart_puts("dhcp dns fail\n");
        return -1;
    }
    uart_puts("dhcp ok\n");
    pm_metal_net_face_mark(PM_METAL_NET_FACE_DHCP);

    if (pm_metal_ip_announce() != 0) {
        uart_puts("ip announce fail\n");
        return -1;
    }
    for (i = 0; i < 64; i++) {
        pm_metal_ip_poll();
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
        return -1;
    }
    if (tftp_smoke() != 0) {
        return -1;
    }
    /* Re-arm LISTEN so outbound client proves the server PCB is independent. */
    if (pm_metal_tcp_listen(22) != 0) {
        uart_puts("tcp dual listen fail\n");
        return -1;
    }
    if (ssh_client_smoke() != 0) {
        return -1;
    }

    uart_puts("ip ok\n");
    return 0;
}
