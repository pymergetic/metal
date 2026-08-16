/* pymergetic.metal.net.dns — A lookup + tiny zone on ip UDP. */
#include "pymergetic/metal/net/dns/__exports__.h"

#include "pymergetic/metal/net/ip.h"

#include <string.h>

#define ZONE_MAX 8
#define NAME_MAX 80
#define DNS_MAX 512

struct zone {
    uint32_t used;
    char name[NAME_MAX];
    uint32_t addr_be;
};

static pm_util_mem_arena_t *s_arena;
static struct zone s_zone[ZONE_MAX];
static int32_t s_fd = -1;
static uint16_t s_xid = 1;

static uint32_t name_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != 0 && b[i] != 0 && i < NAME_MAX; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return a[i] == 0 && b[i] == 0;
}

static int32_t enc_name(uint8_t *out, uint32_t cap, const char *name) {
    uint32_t n = 0;
    while (*name) {
        const char *dot = name;
        uint32_t lab = 0;
        while (*dot && *dot != '.') {
            dot++;
            lab++;
        }
        if (lab == 0 || lab > 63u || n + 1u + lab + 1u > cap) {
            return -1;
        }
        out[n++] = (uint8_t)lab;
        memcpy(out + n, name, lab);
        n += lab;
        name = *dot == '.' ? dot + 1 : dot;
    }
    if (n + 1u > cap) {
        return -1;
    }
    out[n++] = 0;
    return (int32_t)n;
}

static int32_t dec_name(const uint8_t *msg, uint32_t len, uint32_t *off, char *out, uint32_t ocap) {
    uint32_t n = 0;
    uint32_t jumps = 0;
    uint32_t o = *off;
    uint32_t end = o;
    uint32_t saw_ptr = 0;
    while (o < len) {
        uint8_t lab = msg[o];
        if ((lab & 0xc0u) == 0xc0u) {
            if (o + 1u >= len || jumps++ > 8u) {
                return -1;
            }
            if (!saw_ptr) {
                end = o + 2u;
                saw_ptr = 1;
            }
            o = ((uint32_t)(lab & 0x3fu) << 8) | msg[o + 1u];
            continue;
        }
        o++;
        if (lab == 0) {
            if (!saw_ptr) {
                end = o;
            }
            if (n < ocap) {
                out[n] = 0;
            }
            *off = end;
            return 0;
        }
        if (o + lab > len || n + lab + 1u >= ocap) {
            return -1;
        }
        if (n != 0) {
            out[n++] = '.';
        }
        memcpy(out + n, msg + o, lab);
        n += lab;
        o += lab;
    }
    return -1;
}

static uint32_t zone_find(const char *name) {
    uint32_t i;
    for (i = 0; i < ZONE_MAX; i++) {
        if (s_zone[i].used && name_eq(s_zone[i].name, name)) {
            return i;
        }
    }
    return ZONE_MAX;
}

static void dns_reply(const uint8_t *q, uint32_t qlen, uint32_t src, uint16_t sport) {
    char name[NAME_MAX];
    uint8_t out[DNS_MAX];
    uint32_t off;
    uint32_t zi;
    uint32_t n;
    if (qlen < 12u || s_fd < 0) {
        return;
    }
    off = 12;
    if (dec_name(q, qlen, &off, name, sizeof(name)) != 0) {
        return;
    }
    zi = zone_find(name);
    if (zi >= ZONE_MAX) {
        return;
    }
    if (qlen > DNS_MAX - 16u) {
        return;
    }
    memcpy(out, q, qlen);
    out[2] = (uint8_t)(out[2] | 0x80u);
    out[3] = 0;
    out[6] = 0;
    out[7] = 1;
    n = qlen;
    out[n++] = 0xc0;
    out[n++] = 0x0c;
    out[n++] = 0;
    out[n++] = 1;
    out[n++] = 0;
    out[n++] = 1;
    out[n++] = 0;
    out[n++] = 0;
    out[n++] = 0;
    out[n++] = 8;
    out[n++] = 0;
    out[n++] = 4;
    out[n++] = (uint8_t)(s_zone[zi].addr_be >> 24);
    out[n++] = (uint8_t)(s_zone[zi].addr_be >> 16);
    out[n++] = (uint8_t)(s_zone[zi].addr_be >> 8);
    out[n++] = (uint8_t)s_zone[zi].addr_be;
    (void)pm_metal_net_ip_sendto(s_fd, out, n, src, sport);
}

int32_t pm_metal_net_dns_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_zone, 0, sizeof(s_zone));
    s_fd = -1;
    return 0;
}

void pm_metal_net_dns_deinit(void) {
    if (s_fd >= 0) {
        (void)pm_metal_net_ip_close(s_fd);
        s_fd = -1;
    }
    s_arena = NULL;
}

int32_t pm_metal_net_dns_add(const char *name, uint32_t addr_be) {
    uint32_t i;
    if (name == NULL || name[0] == 0) {
        return -1;
    }
    for (i = 0; i < ZONE_MAX; i++) {
        if (!s_zone[i].used) {
            uint32_t n = 0;
            while (name[n] != 0 && n + 1u < NAME_MAX) {
                s_zone[i].name[n] = name[n];
                n++;
            }
            s_zone[i].name[n] = 0;
            s_zone[i].addr_be = addr_be;
            s_zone[i].used = 1;
            return 0;
        }
    }
    return -1;
}

int32_t pm_metal_net_dns_listen(uint32_t addr_be, uint16_t port) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_fd >= 0) {
        return 0;
    }
    s_fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (s_fd < 0 || pm_metal_net_ip_bind(s_fd, addr_be, port) != 0) {
        if (s_fd >= 0) {
            (void)pm_metal_net_ip_close(s_fd);
            s_fd = -1;
        }
        return -1;
    }
    return 0;
}

int32_t pm_metal_net_dns_poll(void) {
    uint8_t buf[DNS_MAX];
    uint32_t addr = 0;
    uint16_t port = 0;
    int32_t n;
    if (s_fd < 0) {
        return 0;
    }
    n = pm_metal_net_ip_recvfrom(s_fd, buf, sizeof(buf), &addr, &port);
    if (n == 0) {
        return 0;
    }
    if (n > 0) {
        dns_reply(buf, (uint32_t)n, addr, port);
    }
    return 0;
}

int32_t pm_metal_net_dns_lookup(const char *name, uint32_t server_be, uint16_t server_port, uint32_t *out_be) {
    int32_t fd;
    uint8_t q[DNS_MAX];
    uint8_t buf[DNS_MAX];
    int32_t nl;
    uint16_t xid;
    int32_t n;
    uint32_t off;
    char qn[NAME_MAX];
    uint32_t addr = 0;
    uint16_t port = 0;
    if (s_arena == NULL || name == NULL || out_be == NULL) {
        return -1;
    }
    fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (fd < 0 || pm_metal_net_ip_bind(fd, 0x7f000001u, 5354) != 0) {
        if (fd >= 0) {
            (void)pm_metal_net_ip_close(fd);
        }
        return -1;
    }
    memset(q, 0, sizeof(q));
    xid = s_xid++;
    q[0] = (uint8_t)(xid >> 8);
    q[1] = (uint8_t)xid;
    q[2] = 0x01;
    q[5] = 1;
    nl = enc_name(q + 12, (uint32_t)(sizeof(q) - 16u), name);
    if (nl < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    q[12 + (uint32_t)nl] = 0;
    q[13 + (uint32_t)nl] = 1;
    q[14 + (uint32_t)nl] = 0;
    q[15 + (uint32_t)nl] = 1;
    if (pm_metal_net_ip_sendto(fd, q, 12u + (uint32_t)nl + 4u, server_be, server_port) < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    (void)pm_metal_net_dns_poll();
    n = pm_metal_net_ip_recvfrom(fd, buf, sizeof(buf), &addr, &port);
    (void)pm_metal_net_ip_close(fd);
    if (n < 12) {
        return -1;
    }
    if (((uint16_t)((buf[0] << 8) | buf[1])) != xid || (buf[2] & 0x80u) == 0 || buf[7] == 0) {
        return -1;
    }
    off = 12;
    if (dec_name(buf, (uint32_t)n, &off, qn, sizeof(qn)) != 0) {
        return -1;
    }
    off += 4;
    if (off + 2u < (uint32_t)n && buf[off] == 0xc0) {
        off += 2;
    } else if (dec_name(buf, (uint32_t)n, &off, qn, sizeof(qn)) != 0) {
        return -1;
    }
    if (off + 14u > (uint32_t)n) {
        return -1;
    }
    off += 10;
    *out_be = ((uint32_t)buf[off] << 24) | ((uint32_t)buf[off + 1] << 16) | ((uint32_t)buf[off + 2] << 8)
        | buf[off + 3];
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.dns, pm_metal_net_dns_init, pm_metal_net_dns_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dns, pm_metal_net_dns_deinit, pm_metal_net_dns_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.dns, pm_metal_net_dns_add, pm_metal_net_dns_add, int32_t(const char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dns, pm_metal_net_dns_listen, pm_metal_net_dns_listen, int32_t(uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dns, pm_metal_net_dns_poll, pm_metal_net_dns_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.dns, pm_metal_net_dns_lookup, pm_metal_net_dns_lookup, int32_t(const char *, uint32_t, uint16_t, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.net.dns, pm_metal_net_dns_init, pm_metal_net_dns_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.dns, pymergetic.metal.net.ip);
