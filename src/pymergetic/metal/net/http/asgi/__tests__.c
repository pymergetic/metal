/* pymergetic.metal.net.http.asgi — parked io.fetch to RS listen on lo. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/http/asgi.h"
#include "pymergetic/metal/net/ip.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define ASGI_PORT 8090

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.http.asgi test: %s\n", why);
    return 1;
}

static int32_t case_fetch_default(void) {
    _Static_assert(sizeof(pm_metal_async_coro_t) == 40, "asgi CoroHead");
    if (pm_metal_net_http_asgi_listen(LO4, ASGI_PORT) != 0) {
        return fail("listen");
    }
    pm_metal_async_poll();
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st = pm_metal_wasm_io_fetch("http://127.0.0.1:8090/x", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch");
    }
    if (n != 4 || body == NULL || memcmp(body, "asgi", 4) != 0) {
        return fail("body");
    }
    return 0;
}

static int32_t case_route(void) {
    const uint8_t hello[] = "hello";
    if (pm_metal_net_http_asgi_route("GET", "/hi", hello, 5) != 0) {
        return fail("route");
    }
    pm_metal_async_poll();
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st = pm_metal_wasm_io_fetch("http://127.0.0.1:8090/hi", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch route");
    }
    if (n != 5 || body == NULL || memcmp(body, "hello", 5) != 0) {
        return fail("route body");
    }
    return 0;
}

int32_t pm_metal_net_http_asgi_tests(void) {
    if (case_fetch_default() != 0) {
        return 1;
    }
    if (case_route() != 0) {
        return 1;
    }
    return 0;
}
