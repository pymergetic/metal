#include "pymergetic/metal/net/ntp/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/dns/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/sock.h"

#define NTP_PORT 123u
#define NTP_CLIENT_PORT 49523u
#define NTP_PACKET_LEN 48u
#define NTP_UNIX_DELTA 2208988800u /* 1970 - 1900 */

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

int32_t pm_metal_net_ntp_query(uint32_t server_ip, uint32_t *unix_secs_out)
{
    uint8_t req[NTP_PACKET_LEN];
    uint8_t rx[NTP_PACKET_LEN + 16u];
    uint32_t rx_len;
    uint32_t src_ip;
    uint16_t src_port;
    uint32_t ntp_secs;
    int attempt;
    int i;
    int32_t rc;
    int got = 0;

    if (server_ip == 0u || unix_secs_out == NULL) {
        return -1;
    }
    if (!pm_metal_net_ip_ready()) {
        return -1;
    }

    memset(req, 0, sizeof(req));
    /* LI=0 VN=4 Mode=3 (client) */
    req[0] = 0x1bu;

    {
        pm_metal_net_ip_sock_h uh =
            pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_DGRAM);
        if (uh == PM_METAL_NET_IP_SOCK_INVALID) {
            return -1;
        }
        if (pm_metal_net_ip_bind(uh, NTP_CLIENT_PORT) != 0) {
            pm_metal_net_ip_close(uh);
            return -1;
        }
        if (arp_wait(server_ip, 256) != 0) {
            if (arp_wait(PM_METAL_NET_IP_DEFAULT_GW, 256) != 0) {
                pm_metal_net_ip_close(uh);
                return -1;
            }
        }

        for (attempt = 0; attempt < 4 && !got; attempt++) {
            rc = (int32_t)pm_metal_net_ip_sendto_ip4(uh, server_ip, NTP_PORT, req, NTP_PACKET_LEN);
            if (rc == 0) {
                pm_metal_net_ip_close(uh);
                return -1;
            }
            for (i = 0; i < 8000; i++) {
                pm_metal_net_ip_poll();
                if (pm_metal_net_ip_try_recvfrom_ip4(uh, &src_ip, &src_port, rx, sizeof(rx),
                                                     &rx_len) == 1) {
                    if (src_port == NTP_PORT && rx_len >= NTP_PACKET_LEN &&
                        (rx[0] & 0x07u) == 4u /* server mode */) {
                        ntp_secs = ((uint32_t)rx[40] << 24) | ((uint32_t)rx[41] << 16) |
                                   ((uint32_t)rx[42] << 8) | (uint32_t)rx[43];
                        if (ntp_secs > NTP_UNIX_DELTA) {
                            *unix_secs_out = ntp_secs - NTP_UNIX_DELTA;
                            got = 1;
                            break;
                        }
                    }
                }
            }
        }
        pm_metal_net_ip_close(uh);
    }
    return got ? 0 : -2;
}

int32_t pm_metal_net_ntp_query_host(const char *host, uint32_t *unix_secs_out)
{
    uint32_t addr = 0;

    if (host == NULL || unix_secs_out == NULL) {
        return -1;
    }
    if (pm_metal_net_dns_resolve(host, &addr) != 0 || addr == 0u) {
        return -1;
    }
    return pm_metal_net_ntp_query(addr, unix_secs_out);
}
