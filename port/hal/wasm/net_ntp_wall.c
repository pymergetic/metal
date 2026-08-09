/*
 * Browser net.ntp — same C ABI; wall clock via Date.now (no UDP SNTP).
 */
#include "pymergetic/metal/net/ntp/__init__.h"
#include "pymergetic/metal/async/handle.h"

#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static int32_t g_status; /* 1 ok, 0 pending/fail, -1 error */
static uint32_t g_last_unix;

#if defined(__EMSCRIPTEN__)
EM_JS(uint32_t, pm_metal_wasm_js_unix_secs, (void), {
    return (Math.floor(Date.now() / 1000)) >>> 0;
});
#endif

static uint32_t wall_unix_secs(void)
{
#if defined(__EMSCRIPTEN__)
    return pm_metal_wasm_js_unix_secs();
#else
    return 0u;
#endif
}

static uint32_t finish_ok(void)
{
    g_last_unix = wall_unix_secs();
    g_status = (g_last_unix > 0u) ? 1 : -1;
    return pm_metal_async_completed_u32(g_status == 1 ? 1u : 0u);
}

uint32_t pm_metal_net_ntp_sync(uint32_t server_ip)
{
    (void)server_ip;
    return finish_ok();
}

uint32_t pm_metal_net_ntp_sync_host(const char *host)
{
    (void)host;
    return finish_ok();
}

void pm_metal_net_ntp_poll(void) {}

int32_t pm_metal_net_ntp_status(void)
{
    return g_status;
}

uint32_t pm_metal_net_ntp_last_unix_secs(void)
{
    return g_last_unix;
}

int32_t pm_metal_net_ntp_query(uint32_t server_ip, uint32_t *unix_secs_out)
{
    uint32_t h = pm_metal_net_ntp_sync(server_ip);

    if (pm_metal_async_result_u32(h) == 0u) {
        return -1;
    }
    if (unix_secs_out) {
        *unix_secs_out = g_last_unix;
    }
    return 0;
}

int32_t pm_metal_net_ntp_query_host(const char *host, uint32_t *unix_secs_out)
{
    uint32_t h = pm_metal_net_ntp_sync_host(host);

    if (pm_metal_async_result_u32(h) == 0u) {
        return -1;
    }
    if (unix_secs_out) {
        *unix_secs_out = g_last_unix;
    }
    return 0;
}
