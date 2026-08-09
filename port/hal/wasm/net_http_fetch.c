/*
 * Browser net.http — same C ABI; GET via js.fetch (Emscripten Asyncify).
 * Server listen APIs are no-ops (no TCP accept in the browser seat).
 */
#include "pymergetic/metal/net/http/__init__.h"
#include "pymergetic/metal/async/handle.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#ifndef PM_METAL_WASM_HTTP_BODY_MAX
#define PM_METAL_WASM_HTTP_BODY_MAX (256u * 1024u)
#endif

static uint8_t g_body[PM_METAL_WASM_HTTP_BODY_MAX];
static uint32_t g_body_len;
static uint32_t g_status;
static int32_t g_tls_verify_none;

#if defined(__EMSCRIPTEN__)
/* 0 = transport ok (status/body filled); -1 = fetch failed. */
EM_ASYNC_JS(int, pm_metal_wasm_js_http_get,
            (const char *uri, uint8_t *out_buf, uint32_t out_cap, uint32_t *out_len,
             uint32_t *out_status),
            {
                try {
                    const url = UTF8ToString(uri);
                    const resp = await fetch(url, {
                        credentials : "same-origin",
                        cache : "no-store",
                    });
                    setValue(out_status, resp.status, "i32");
                    const ab = await resp.arrayBuffer();
                    const u8 = new Uint8Array(ab);
                    const n = u8.length > out_cap ? out_cap : u8.length;
                    if (n > 0) {
                        HEAPU8.set(u8.subarray(0, n), out_buf);
                    }
                    setValue(out_len, n, "i32");
                    return 0;
                } catch (e) {
                    return -1;
                }
            });
#endif

int32_t pm_metal_net_http_init(void)
{
    return -1; /* no TCP :80 listen on browser */
}

int32_t pm_metal_net_http_init_tls(void)
{
    return -1; /* no TCP :443 listen on browser */
}

void pm_metal_net_http_shutdown(void) {}

int32_t pm_metal_net_http_poll(void)
{
    return 0;
}

int32_t pm_metal_net_http_served(void)
{
    return 0;
}

void pm_metal_net_http_set_tls_verify_none(int32_t on)
{
    g_tls_verify_none = on ? 1 : 0;
}

uint32_t pm_metal_net_http_status(void)
{
    return g_status;
}

uint32_t pm_metal_net_http_body_len(void)
{
    return g_body_len;
}

const uint8_t *pm_metal_net_http_body(void)
{
    return g_body_len ? g_body : NULL;
}

void pm_metal_net_http_client_poll(void)
{
    /* Fetch completes inside get(); nothing to advance. */
    (void)g_tls_verify_none;
}

uint32_t pm_metal_net_http_get(const char *url)
{
    uint32_t n = 0;
    uint32_t st = 0;
    int rc;

    g_body_len = 0;
    g_status = 0;
    if (url == NULL || url[0] == '\0') {
        return pm_metal_async_completed_u32(0u);
    }
#if defined(__EMSCRIPTEN__)
    rc = pm_metal_wasm_js_http_get(url, g_body, (uint32_t)sizeof(g_body), &n, &st);
#else
    (void)n;
    (void)st;
    rc = -1;
#endif
    if (rc != 0) {
        return pm_metal_async_completed_u32(0u);
    }
    g_body_len = n;
    g_status = st;
    return pm_metal_async_completed_u32(st >= 100u && st < 600u ? 1u : 0u);
}

int32_t pm_metal_net_http_client_get(const char *host, uint16_t port, const char *path,
                                     uint8_t *buf, uint32_t cap, uint32_t *len_out)
{
    char url[384];
    uint32_t h;
    int n;

    if (host == NULL || path == NULL) {
        return -1;
    }
    if (port == 0u || port == 80u) {
        n = snprintf(url, sizeof(url), "http://%s%s", host, path[0] ? path : "/");
    } else if (port == 443u) {
        n = snprintf(url, sizeof(url), "https://%s%s", host, path[0] ? path : "/");
    } else {
        n = snprintf(url, sizeof(url), "http://%s:%u%s", host, (unsigned)port,
                     path[0] ? path : "/");
    }
    if (n <= 0 || (size_t)n >= sizeof(url)) {
        return -1;
    }
    h = pm_metal_net_http_get(url);
    if (pm_metal_async_result_u32(h) == 0u) {
        return -1;
    }
    if (buf != NULL && cap > 0u && g_body_len > 0u) {
        uint32_t copy = g_body_len < cap ? g_body_len : cap;
        memcpy(buf, g_body, copy);
        if (len_out) {
            *len_out = copy;
        }
    } else if (len_out) {
        *len_out = g_body_len;
    }
    return 0;
}
