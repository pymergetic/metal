#include "pymergetic/metal/net/dns.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/internal.h"
#include "pymergetic/metal/net/ip/udp.h"

#define DNS_PORT 53u
#define DNS_CLIENT_PORT 49510u

static uint16_t g_xid = 0xA1B2u;

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int parse_dotted_ipv4(const char *s, uint32_t *addr_out)
{
    unsigned o = 0;
    unsigned v = 0;
    int digit = 0;
    size_t i = 0;
    uint8_t oct[4];

    if (s == NULL || addr_out == NULL || s[0] == '\0') {
        return -1;
    }
    for (;;) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            v = v * 10u + (unsigned)(c - '0');
            if (v > 255u) {
                return -1;
            }
            digit = 1;
            i++;
        } else if (c == '.' || c == '\0') {
            if (!digit || o >= 4u) {
                return -1;
            }
            oct[o++] = (uint8_t)v;
            v = 0;
            digit = 0;
            if (c == '\0') {
                break;
            }
            i++;
        } else {
            return -1;
        }
    }
    if (o != 4u) {
        return -1;
    }
    *addr_out = ((uint32_t)oct[0] << 24) | ((uint32_t)oct[1] << 16) | ((uint32_t)oct[2] << 8) |
                (uint32_t)oct[3];
    return 0;
}

static int encode_name(uint8_t *out, size_t cap, const char *name, size_t *len_out)
{
    size_t i = 0;
    size_t o = 0;

    if (out == NULL || name == NULL || len_out == NULL || cap < 2u) {
        return -1;
    }
    while (name[i] != '\0') {
        size_t label = 0;
        size_t start = i;
        while (name[i] != '\0' && name[i] != '.') {
            i++;
            label++;
        }
        if (label == 0u || label > 63u || o + 1u + label >= cap) {
            return -1;
        }
        out[o++] = (uint8_t)label;
        memcpy(out + o, name + start, label);
        o += label;
        if (name[i] == '.') {
            i++;
            if (name[i] == '\0') {
                return -1;
            }
        }
    }
    if (o + 1u >= cap) {
        return -1;
    }
    out[o++] = 0;
    *len_out = o;
    return 0;
}

static int skip_name(const uint8_t *buf, uint32_t len, uint32_t *pos)
{
    uint32_t p;

    if (buf == NULL || pos == NULL) {
        return -1;
    }
    p = *pos;
    while (p < len) {
        uint8_t lab = buf[p];
        if (lab == 0u) {
            *pos = p + 1u;
            return 0;
        }
        if ((lab & 0xc0u) == 0xc0u) {
            if (p + 1u >= len) {
                return -1;
            }
            *pos = p + 2u;
            return 0;
        }
        if ((uint32_t)p + 1u + lab > len) {
            return -1;
        }
        p += 1u + lab;
    }
    return -1;
}

static int parse_a_answer(const uint8_t *buf, uint32_t len, uint16_t xid, uint32_t *addr_out)
{
    uint16_t qdcount;
    uint16_t ancount;
    uint32_t pos;
    uint16_t i;

    if (buf == NULL || addr_out == NULL || len < 12u) {
        return -1;
    }
    if (get_u16(buf) != xid) {
        return -1;
    }
    if ((buf[2] & 0x80u) == 0u) {
        return -1;
    }
    if ((buf[3] & 0x0fu) != 0u) {
        return -1;
    }
    qdcount = get_u16(buf + 4);
    ancount = get_u16(buf + 6);
    pos = 12u;
    for (i = 0; i < qdcount; i++) {
        if (skip_name(buf, len, &pos) != 0) {
            return -1;
        }
        if (pos + 4u > len) {
            return -1;
        }
        pos += 4u;
    }
    for (i = 0; i < ancount; i++) {
        uint16_t typ;
        uint16_t cls;
        uint16_t rdlen;

        if (skip_name(buf, len, &pos) != 0) {
            return -1;
        }
        if (pos + 10u > len) {
            return -1;
        }
        typ = get_u16(buf + pos);
        cls = get_u16(buf + pos + 2);
        rdlen = get_u16(buf + pos + 8);
        pos += 10u;
        if (pos + rdlen > len) {
            return -1;
        }
        if (typ == 1u && cls == 1u && rdlen == 4u) {
            *addr_out = get_u32(buf + pos);
            return 0;
        }
        pos += rdlen;
    }
    return -1;
}

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

int32_t pm_metal_net_dns_resolve(const char *name, uint32_t *addr_out)
{
    uint8_t q[256];
    uint8_t rx[512];
    size_t name_len = 0;
    uint16_t xid;
    uint32_t server;
    uint32_t rx_len;
    uint32_t src_ip;
    uint16_t src_port;
    uint32_t addr = 0;
    int attempt;
    int i;
    int32_t rc;
    int got = 0;

    if (name == NULL || addr_out == NULL || name[0] == '\0') {
        return -1;
    }
    if (parse_dotted_ipv4(name, addr_out) == 0) {
        return 0;
    }
    if (!pm_metal_net_ip_ready()) {
        return -1;
    }
    server = pm_metal_net_ip_dns();
    if (server == 0u) {
        server = PM_METAL_NET_IP_DEFAULT_DNS;
    }

    memset(q, 0, sizeof(q));
    g_xid = (uint16_t)(g_xid + 1u);
    if (g_xid == 0u) {
        g_xid = 1u;
    }
    xid = g_xid;
    q[0] = (uint8_t)(xid >> 8);
    q[1] = (uint8_t)xid;
    q[2] = 0x01u;
    q[3] = 0x00u;
    q[4] = 0x00u;
    q[5] = 0x01u;
    if (encode_name(q + 12, sizeof(q) - 16u, name, &name_len) != 0) {
        return -1;
    }
    q[12u + name_len] = 0x00u;
    q[13u + name_len] = 0x01u;
    q[14u + name_len] = 0x00u;
    q[15u + name_len] = 0x01u;

    if (pm_metal_net_ip_udp_bind(DNS_CLIENT_PORT) != 0) {
        return -1;
    }
    if (arp_wait(server, 256) != 0) {
        if (arp_wait(PM_METAL_NET_IP_DEFAULT_GW, 256) != 0) {
            return -1;
        }
    }

    for (attempt = 0; attempt < 4 && !got; attempt++) {
        for (i = 0; i < 16; i++) {
            rc = pm_metal_net_ip_udp_sendto(server, DNS_PORT, q, (uint32_t)(16u + name_len));
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
        for (i = 0; i < 6000; i++) {
            pm_metal_net_ip_poll();
            if (pm_metal_net_ip_udp_recv(&src_ip, &src_port, rx, sizeof(rx), &rx_len) == 1) {
                if (src_port == DNS_PORT && parse_a_answer(rx, rx_len, xid, &addr) == 0) {
                    got = 1;
                    break;
                }
            }
        }
    }
    if (!got) {
        return -2;
    }
    *addr_out = addr;
    return 0;
}
