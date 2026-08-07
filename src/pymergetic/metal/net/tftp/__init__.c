#include "pymergetic/metal/net/tftp.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/internal.h"
#include "pymergetic/metal/net/ip/udp.h"

#define TFTP_PORT 69u
#define TFTP_CLIENT_PORT 49569u
#define TFTP_RRQ 1u
#define TFTP_DATA 3u
#define TFTP_ACK 4u
#define TFTP_ERROR 5u

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

int32_t pm_metal_tftp_get(uint32_t server_ip, const char *filename,
                          uint8_t *buf, uint32_t cap, uint32_t *len_out)
{
    uint8_t req[256];
    uint8_t rx[600];
    uint8_t ack[4];
    uint32_t rx_len;
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t tid = 0;
    size_t nlen;
    size_t o;
    int attempt;
    int i;
    int32_t rc;
    int got = 0;

    if (server_ip == 0u || filename == NULL || buf == NULL || len_out == NULL || cap == 0u) {
        return -1;
    }
    *len_out = 0;
    if (!pm_metal_net_ip_ready()) {
        return -1;
    }
    nlen = 0;
    while (filename[nlen] != '\0') {
        nlen++;
    }
    if (nlen == 0u || nlen > 128u) {
        return -1;
    }

    /* RRQ: opcode, filename, 0, "octet", 0 */
    put_u16(req, TFTP_RRQ);
    o = 2;
    memcpy(req + o, filename, nlen);
    o += nlen;
    req[o++] = 0;
    memcpy(req + o, "octet", 5);
    o += 5;
    req[o++] = 0;

    if (pm_metal_net_ip_udp_bind(TFTP_CLIENT_PORT) != 0) {
        return -1;
    }
    if (arp_wait(server_ip, 256) != 0) {
        if (arp_wait(PM_METAL_NET_IP_DEFAULT_GW, 256) != 0) {
            return -1;
        }
    }

    for (attempt = 0; attempt < 4 && !got; attempt++) {
        for (i = 0; i < 16; i++) {
            rc = pm_metal_net_ip_udp_sendto(server_ip, TFTP_PORT, req, (uint32_t)o);
            if (rc == 0) {
                break;
            }
            if (rc != -2) {
                return -1;
            }
            pm_metal_net_ip_poll();
        }
        if (rc != 0) {
            return -1;
        }
        for (i = 0; i < 8000; i++) {
            pm_metal_net_ip_poll();
            if (pm_metal_net_ip_udp_recv(&src_ip, &src_port, rx, sizeof(rx), &rx_len) != 1) {
                continue;
            }
            if (src_ip != server_ip || rx_len < 4u) {
                continue;
            }
            if (get_u16(rx) == TFTP_ERROR) {
                return -1;
            }
            if (get_u16(rx) != TFTP_DATA || get_u16(rx + 2) != 1u) {
                continue;
            }
            tid = src_port;
            {
                uint32_t payload = rx_len - 4u;
                if (payload > cap) {
                    payload = cap;
                }
                memcpy(buf, rx + 4, payload);
                *len_out = payload;
            }
            put_u16(ack, TFTP_ACK);
            put_u16(ack + 2, 1u);
            for (i = 0; i < 16; i++) {
                rc = pm_metal_net_ip_udp_sendto(server_ip, tid, ack, 4u);
                if (rc == 0 || rc != -2) {
                    break;
                }
                pm_metal_net_ip_poll();
            }
            got = 1;
            break;
        }
    }
    return got ? 0 : -2;
}
