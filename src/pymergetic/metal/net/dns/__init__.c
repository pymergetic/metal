#include "pymergetic/metal/net/dns/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/ip/cfg.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/pump/__init__.h"

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_net_dns_reg_load. */
static pm_metal_reg_export_t net_dns_exports[] = {
    PM_METAL_REG_EXPORT(lookup),
    PM_METAL_REG_EXPORT(last_addr),
    PM_METAL_REG_EXPORT(resolve),
};
PM_METAL_REG_REF(net_dns, lookup, 0);
PM_METAL_REG_REF(net_dns, last_addr, 1);
PM_METAL_REG_REF(net_dns, resolve, 2);
PM_METAL_REG_MOD(net_dns, "pymergetic.metal.net.dns")

static int32_t net_dns_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(net_dns_lookup, (void *)pm_metal_net_dns_lookup);
    pm_metal_reg_export_publish(net_dns_last_addr, (void *)pm_metal_net_dns_last_addr);
    pm_metal_reg_export_publish(net_dns_resolve, (void *)pm_metal_net_dns_resolve);
    return 0;
}

#ifndef PM_METAL_DNS_WAIT_ITERS
#define PM_METAL_DNS_WAIT_ITERS 20000u
#endif

static uint32_t g_last_addr;

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

uint32_t pm_metal_net_dns_lookup(const char *name)
{
    /* Drop sticky cache so last_addr() reflects this lookup's ntoa. */
    g_last_addr = 0;
    return pm_metal_net_ip_dns_lookup(name);
}

uint32_t pm_metal_net_dns_last_addr(void)
{
    char buf[64];

    if (g_last_addr != 0u) {
        return g_last_addr;
    }
    if (pm_metal_net_ip_dns_last_ntoa(buf, sizeof buf) != 0) {
        return 0;
    }
    if (parse_dotted_ipv4(buf, &g_last_addr) != 0) {
        return 0;
    }
    return g_last_addr;
}

int32_t pm_metal_net_dns_resolve(const char *name, uint32_t *addr_out)
{
    uint32_t h;
    uint32_t i;

    if (name == NULL || addr_out == NULL || name[0] == '\0') {
        return -1;
    }
    g_last_addr = 0;
    if (parse_dotted_ipv4(name, addr_out) == 0) {
        g_last_addr = *addr_out;
        return 0;
    }

    h = pm_metal_net_dns_lookup(name);
    if (h == 0u) {
        return -1;
    }
    for (i = 0; i < PM_METAL_DNS_WAIT_ITERS; i++) {
        pm_metal_net_pump_once();
        pm_metal_board_time_advance_us(1000);
        (void)pm_metal_async_run_poll();
        if (pm_metal_async_status(h) == PM_METAL_ASYNC_DONE) {
            break;
        }
    }
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE) {
        pm_metal_async_coro_close(h);
        return -2;
    }
    if (pm_metal_async_result_u32(h) != 1u) {
        pm_metal_async_coro_close(h);
        return -1;
    }
    pm_metal_async_coro_close(h);
    *addr_out = pm_metal_net_dns_last_addr();
    if (*addr_out == 0u) {
        return -1;
    }
    return 0;
}
