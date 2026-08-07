#include "pymergetic/metal/net/dhcp.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/dev/net.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ip_internal.h"
#include "pymergetic/metal/net/udp.h"

#define DHCP_SERVER_PORT 67u
#define DHCP_CLIENT_PORT 68u
#define DHCP_MAGIC       0x63825363u
#define DHCP_OP_BOOTREQUEST 1u
#define DHCP_OP_BOOTREPLY   2u
#define DHCP_DISCOVER 1u
#define DHCP_OFFER    2u
#define DHCP_REQUEST  3u
#define DHCP_ACK      5u

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t parse_options(const uint8_t *opt, uint32_t opt_len, uint8_t *msgtype,
                              uint32_t *mask, uint32_t *gw, uint32_t *dns, uint32_t *server)
{
    uint32_t i = 0;

    if (msgtype != NULL) {
        *msgtype = 0;
    }
    while (i < opt_len) {
        uint8_t code = opt[i++];
        uint8_t len;
        if (code == 0xffu) {
            break;
        }
        if (code == 0u) {
            continue;
        }
        if (i >= opt_len) {
            break;
        }
        len = opt[i++];
        if ((uint32_t)i + len > opt_len) {
            break;
        }
        if (code == 53u && len >= 1u && msgtype != NULL) {
            *msgtype = opt[i];
        } else if (code == 1u && len >= 4u && mask != NULL) {
            *mask = get_u32(opt + i);
        } else if (code == 3u && len >= 4u && gw != NULL) {
            *gw = get_u32(opt + i);
        } else if (code == 6u && len >= 4u && dns != NULL) {
            *dns = get_u32(opt + i);
        } else if (code == 54u && len >= 4u && server != NULL) {
            *server = get_u32(opt + i);
        }
        i += len;
    }
    return 0;
}

static int32_t dhcp_build(uint8_t *pkt, uint32_t cap, uint32_t xid, uint8_t msgtype,
                          uint32_t req_ip, uint32_t server_ip)
{
    const uint8_t *mac = pm_metal_ip_mac();
    uint32_t o;

    if (pkt == NULL || cap < 300u || mac == NULL) {
        return -1;
    }
    memset(pkt, 0, 300);
    pkt[0] = DHCP_OP_BOOTREQUEST;
    pkt[1] = 1; /* HTYPE ethernet */
    pkt[2] = 6; /* hlen */
    put_u32(pkt + 4, xid);
    memcpy(pkt + 28, mac, 6);
    put_u32(pkt + 236, DHCP_MAGIC);
    o = 240;
    pkt[o++] = 53;
    pkt[o++] = 1;
    pkt[o++] = msgtype;
    if (msgtype == DHCP_REQUEST) {
        if (req_ip != 0u) {
            pkt[o++] = 50;
            pkt[o++] = 4;
            put_u32(pkt + o, req_ip);
            o += 4;
        }
        if (server_ip != 0u) {
            pkt[o++] = 54;
            pkt[o++] = 4;
            put_u32(pkt + o, server_ip);
            o += 4;
        }
    }
    pkt[o++] = 55; /* parameter request list */
    pkt[o++] = 3;
    pkt[o++] = 1;
    pkt[o++] = 3;
    pkt[o++] = 6;
    pkt[o++] = 0xff;
    return (int32_t)o;
}

static int32_t dhcp_send(const uint8_t *pkt, uint32_t len)
{
    /* Broadcast UDP with src 0.0.0.0 — checksum 0 permitted for IPv4 UDP. */
    uint8_t seg[512];
    uint32_t seg_len;

    if (pkt == NULL || len == 0u || len + 8u > sizeof(seg)) {
        return -1;
    }
    seg_len = 8u + len;
    put_u16(seg + 0, DHCP_CLIENT_PORT);
    put_u16(seg + 2, DHCP_SERVER_PORT);
    put_u16(seg + 4, (uint16_t)seg_len);
    put_u16(seg + 6, 0);
    memcpy(seg + 8, pkt, len);
    return pm_metal_ip_tx_l4(0xffffffffu, 17, seg, seg_len);
}

static int32_t dhcp_recv(uint32_t xid, uint8_t want_type, pm_metal_dhcp_lease_t *lease)
{
    uint8_t buf[512];
    uint32_t n;
    uint32_t src_ip;
    uint16_t src_port;
    uint8_t msgtype;
    int i;

    for (i = 0; i < 8000; i++) {
        pm_metal_ip_poll();
        if (pm_metal_udp_recv(&src_ip, &src_port, buf, sizeof(buf), &n) != 1) {
            continue;
        }
        if (src_port != DHCP_SERVER_PORT || n < 240u) {
            continue;
        }
        if (buf[0] != DHCP_OP_BOOTREPLY || get_u32(buf + 4) != xid) {
            continue;
        }
        if (get_u32(buf + 236) != DHCP_MAGIC) {
            continue;
        }
        msgtype = 0;
        lease->yiaddr = get_u32(buf + 16);
        lease->mask = 0xffffff00u;
        lease->gw = 0;
        lease->dns = 0;
        lease->server = src_ip;
        parse_options(buf + 240, n - 240u, &msgtype, &lease->mask, &lease->gw, &lease->dns,
                      &lease->server);
        if (msgtype != want_type || lease->yiaddr == 0u) {
            continue;
        }
        if (lease->gw == 0u) {
            lease->gw = (lease->yiaddr & lease->mask) | 0x00000002u;
        }
        if (lease->dns == 0u) {
            lease->dns = (lease->yiaddr & lease->mask) | 0x00000003u;
        }
        (void)get_u16;
        return 0;
    }
    return -1;
}

int32_t pm_metal_dhcp_run(pm_metal_dhcp_lease_t *lease_out)
{
    uint8_t pkt[300];
    int32_t plen;
    uint32_t xid;
    const uint8_t *mac;
    pm_metal_dhcp_lease_t offer;
    pm_metal_dhcp_lease_t ack;
    int attempt;

    if (lease_out == NULL || !pm_metal_ip_ready()) {
        return -1;
    }
    mac = pm_metal_dev_net_virtio_mac();
    if (mac == NULL) {
        return -1;
    }
    xid = 0x4d455441u ^ ((uint32_t)mac[4] << 8) ^ mac[5];

    if (pm_metal_udp_bind(DHCP_CLIENT_PORT) != 0) {
        return -1;
    }

    for (attempt = 0; attempt < 3; attempt++) {
        plen = dhcp_build(pkt, sizeof(pkt), xid, DHCP_DISCOVER, 0, 0);
        if (plen < 0 || dhcp_send(pkt, (uint32_t)plen) != 0) {
            continue;
        }
        memset(&offer, 0, sizeof(offer));
        if (dhcp_recv(xid, DHCP_OFFER, &offer) != 0) {
            continue;
        }

        plen = dhcp_build(pkt, sizeof(pkt), xid, DHCP_REQUEST, offer.yiaddr, offer.server);
        if (plen < 0 || dhcp_send(pkt, (uint32_t)plen) != 0) {
            continue;
        }
        memset(&ack, 0, sizeof(ack));
        if (dhcp_recv(xid, DHCP_ACK, &ack) != 0) {
            continue;
        }
        *lease_out = ack;
        return 0;
    }
    return -1;
}
