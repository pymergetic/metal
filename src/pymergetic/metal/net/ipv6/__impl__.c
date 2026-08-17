/* pymergetic.metal.net.ipv6 — 128-bit address parse/format. */
#include "pymergetic/metal/net/ipv6/__exports__.h"

#include <string.h>

static pm_util_mem_arena_t *s_arena;

static int hexv(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int32_t pm_metal_net_ipv6_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    return 0;
}

void pm_metal_net_ipv6_deinit(void) {
    s_arena = NULL;
}

int32_t pm_metal_net_ipv6_parse(const char *s, uint8_t *out) {
    uint16_t left[8];
    uint16_t right[8];
    uint32_t nl = 0;
    uint32_t nr = 0;
    uint32_t gap = 0;
    uint32_t i;
    const char *p;
    if (s_arena == NULL || s == NULL || out == NULL) {
        return -1;
    }
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    p = s;
    if (p[0] == ':' && p[1] != ':') {
        return -1;
    }
    while (*p != 0) {
        uint32_t v = 0;
        uint32_t digits = 0;
        if (p[0] == ':' && p[1] == ':') {
            if (gap) {
                return -1;
            }
            gap = 1;
            p += 2;
            if (*p == ':') {
                return -1;
            }
            continue;
        }
        if (*p == ':') {
            p++;
        }
        if (*p == 0) {
            break;
        }
        while (hexv(*p) >= 0 && digits < 4u) {
            v = (v << 4) | (uint32_t)hexv(*p);
            p++;
            digits++;
        }
        if (digits == 0) {
            return -1;
        }
        if (*p != 0 && *p != ':') {
            return -1;
        }
        if (!gap) {
            if (nl >= 8u) {
                return -1;
            }
            left[nl++] = (uint16_t)v;
        } else {
            if (nr >= 8u) {
                return -1;
            }
            right[nr++] = (uint16_t)v;
        }
    }
    if (!gap && nl != 8u) {
        return -1;
    }
    if (gap && nl + nr > 8u) {
        return -1;
    }
    memset(out, 0, 16);
    for (i = 0; i < nl; i++) {
        out[i * 2u] = (uint8_t)(left[i] >> 8);
        out[i * 2u + 1u] = (uint8_t)left[i];
    }
    for (i = 0; i < nr; i++) {
        uint32_t o = 8u - nr + i;
        out[o * 2u] = (uint8_t)(right[i] >> 8);
        out[o * 2u + 1u] = (uint8_t)right[i];
    }
    return 0;
}

int32_t pm_metal_net_ipv6_format(const uint8_t *addr, char *out, uint32_t out_max) {
    uint32_t i;
    uint32_t n = 0;
    if (addr == NULL || out == NULL || out_max < 4u) {
        return -1;
    }
    out[0] = 0;
    for (i = 0; i < 8u; i++) {
        uint16_t g = ((uint16_t)addr[i * 2u] << 8) | addr[i * 2u + 1u];
        char tmp[5];
        uint32_t t = 0;
        uint32_t v = g;
        uint32_t k;
        if (i != 0) {
            if (n + 1u >= out_max) {
                return -1;
            }
            out[n++] = ':';
        }
        if (v == 0) {
            tmp[t++] = '0';
        } else {
            char rev[4];
            uint32_t r = 0;
            while (v != 0 && r < 4u) {
                uint32_t d = v & 0xfu;
                rev[r++] = (char)(d < 10u ? '0' + d : 'a' + (d - 10u));
                v >>= 4;
            }
            while (r > 0) {
                tmp[t++] = rev[--r];
            }
        }
        if (n + t >= out_max) {
            return -1;
        }
        for (k = 0; k < t; k++) {
            out[n++] = tmp[k];
        }
    }
    out[n] = 0;
    return 0;
}

int32_t pm_metal_net_ipv6_is_loopback(const uint8_t *addr) {
    uint32_t i;
    if (addr == NULL) {
        return 0;
    }
    for (i = 0; i < 15u; i++) {
        if (addr[i] != 0) {
            return 0;
        }
    }
    return addr[15] == 1 ? 1 : 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.ipv6, pm_metal_net_ipv6_init, pm_metal_net_ipv6_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ipv6, pm_metal_net_ipv6_deinit, pm_metal_net_ipv6_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ipv6, pm_metal_net_ipv6_parse, pm_metal_net_ipv6_parse, int32_t(const char *, uint8_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ipv6, pm_metal_net_ipv6_format, pm_metal_net_ipv6_format, int32_t(const uint8_t *, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ipv6, pm_metal_net_ipv6_is_loopback, pm_metal_net_ipv6_is_loopback, int32_t(const uint8_t *));

PM_MOD_BOOT_C(pymergetic.metal.net.ipv6, pm_metal_net_ipv6_init, pm_metal_net_ipv6_deinit);
