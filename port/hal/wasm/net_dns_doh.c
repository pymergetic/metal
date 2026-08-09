/*
 * Browser net.dns — same C ABI; resolve via DoH (js.fetch + Asyncify).
 */
#include "pymergetic/metal/net/dns/__init__.h"
#include "pymergetic/metal/async/handle.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static uint32_t g_last_addr;

static int parse_dotted_quad(const char *s, uint32_t *out)
{
    unsigned a, b, c, d;
    char tail;

    if (s == NULL || out == NULL) {
        return -1;
    }
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) {
        return -1;
    }
    if (a > 255u || b > 255u || c > 255u || d > 255u) {
        return -1;
    }
    *out = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
    return 0;
}

#if defined(__EMSCRIPTEN__)
/* Returns host-order IPv4, or 0 on failure. */
EM_ASYNC_JS(uint32_t, pm_metal_wasm_js_dns_lookup, (const char *name), {
    try {
        const host = UTF8ToString(name);
        const url = "https://dns.google/resolve?name=" + encodeURIComponent(host) + "&type=A";
        const resp = await fetch(url, {
            credentials : "omit",
            cache : "no-store",
            headers : { Accept : "application/dns-json" },
        });
        if (!resp.ok) {
            return 0;
        }
        const j = await resp.json();
        const ans = j && j.Answer;
        if (!ans || !ans.length) {
            return 0;
        }
        for (let i = 0; i < ans.length; i++) {
            if (ans[i].type === 1 && typeof ans[i].data === "string") {
                const parts = ans[i].data.split(".");
                if (parts.length !== 4) {
                    continue;
                }
                const a = parts.map((p) => parseInt(p, 10));
                if (a.some((n) => Number.isNaN(n) || n < 0 || n > 255)) {
                    continue;
                }
                return ((a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3]) >>> 0;
            }
        }
        return 0;
    } catch (e) {
        return 0;
    }
});
#endif

uint32_t pm_metal_net_dns_lookup(const char *name)
{
    uint32_t addr = 0;

    g_last_addr = 0;
    if (name == NULL || name[0] == '\0') {
        return pm_metal_async_completed_u32(0u);
    }
    if (parse_dotted_quad(name, &addr) == 0) {
        g_last_addr = addr;
        return pm_metal_async_completed_u32(1u);
    }
#if defined(__EMSCRIPTEN__)
    addr = pm_metal_wasm_js_dns_lookup(name);
#else
    addr = 0;
#endif
    if (addr == 0u) {
        return pm_metal_async_completed_u32(0u);
    }
    g_last_addr = addr;
    return pm_metal_async_completed_u32(1u);
}

uint32_t pm_metal_net_dns_last_addr(void)
{
    return g_last_addr;
}

int32_t pm_metal_net_dns_resolve(const char *name, uint32_t *addr_out)
{
    uint32_t h = pm_metal_net_dns_lookup(name);

    if (pm_metal_async_result_u32(h) == 0u) {
        return -1;
    }
    if (addr_out) {
        *addr_out = g_last_addr;
    }
    return 0;
}
