/*
 * Strong overrides for wasmmod ports/metal/io_ops.c weak DECLINE stubs.
 * HTTP(S) URIs park Metal async HTTP GET to completion; other URIs DECLINE.
 */
#include "io_ops.h"

#include <string.h>

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/mem.h>
#include <pymergetic/metal/net/http/__init__.h>
#include <pymergetic/metal/net/pump/__init__.h>

#define METAL_IO_FETCH_CAP 65536u
#define METAL_IO_FETCH_US 15000000ull

static void ensure_ops_table(void)
{
    static int once;
    if (!once) {
        mp_wasm_metal_io_ops_init();
        once = 1;
    }
}

static int uri_is_http(const char *u)
{
    return u != NULL && (strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0);
}

mp_wasm_io_result_t pm_metal_wasm_io_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
                                           char *errbuf, size_t errbuf_len)
{
    uint8_t *buf;
    uint32_t h;
    uint32_t st;
    uint64_t deadline;
    const uint8_t *body;
    uint32_t blen;

    (void)errbuf;
    (void)errbuf_len;
    ensure_ops_table();
    if (!uri_is_http(uri) || out_bytes == NULL || out_len == NULL) {
        return MP_WASM_IO_DECLINE;
    }
    *out_bytes = NULL;
    *out_len = 0;
    buf = pm_metal_mem_alloc(METAL_IO_FETCH_CAP);
    if (buf == NULL) {
        return MP_WASM_IO_ERR;
    }

    h = pm_metal_net_http_get(uri);
    if (h == 0u) {
        pm_metal_mem_free(buf);
        return MP_WASM_IO_ERR;
    }
    if (pm_metal_async_create_task(h) == 0u) {
        pm_metal_async_coro_close(h);
        pm_metal_mem_free(buf);
        return MP_WASM_IO_ERR;
    }
    deadline = pm_metal_time_mono_us() + METAL_IO_FETCH_US;
    for (;;) {
        pm_metal_net_pump_once();
        (void)pm_metal_async_run_poll_all();
        st = (uint32_t)pm_metal_async_status(h);
        if (st == (uint32_t)PM_METAL_ASYNC_DONE) {
            break;
        }
        if (st == (uint32_t)PM_METAL_ASYNC_ERROR || st == (uint32_t)PM_METAL_ASYNC_CANCELLED
            || pm_metal_time_mono_us() >= deadline) {
            pm_metal_async_coro_close(h);
            pm_metal_mem_free(buf);
            return MP_WASM_IO_ERR;
        }
        (void)pm_metal_async_yield();
    }
    blen = pm_metal_net_http_body_len();
    body = pm_metal_net_http_body();
    if (pm_metal_async_result_u32(h) != 1u || pm_metal_net_http_status() != 200u || blen == 0u
        || blen > METAL_IO_FETCH_CAP || body == NULL) {
        pm_metal_async_coro_close(h);
        pm_metal_mem_free(buf);
        return MP_WASM_IO_ERR;
    }
    memcpy(buf, body, (size_t)blen);
    pm_metal_async_coro_close(h);
    *out_bytes = buf;
    *out_len = blen;
    return MP_WASM_IO_OK;
}

mp_wasm_io_result_t pm_metal_wasm_io_probe(const char *uri)
{
    uint8_t *b = NULL;
    uint32_t n = 0;
    mp_wasm_io_result_t r;

    if (!uri_is_http(uri)) {
        return MP_WASM_IO_DECLINE;
    }
    r = pm_metal_wasm_io_fetch(uri, &b, &n, NULL, 0);
    if (r == MP_WASM_IO_OK && b != NULL) {
        pm_metal_mem_free(b);
    }
    return r;
}
