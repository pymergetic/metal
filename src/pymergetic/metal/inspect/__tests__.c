/* pymergetic.metal.inspect — live registry JSON + ASGI fetch. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/inspect.h"
#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/http/asgi.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define INSPECT_PORT 8090

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.inspect test: %s\n", why);
    return 1;
}

static int has(const uint8_t *buf, uint32_t n, const char *needle) {
    size_t nl;
    uint32_t i;
    if (buf == NULL || needle == NULL) {
        return 0;
    }
    nl = strlen(needle);
    if (nl == 0 || n < nl) {
        return 0;
    }
    for (i = 0; i + (uint32_t)nl <= n; i++) {
        if (memcmp(buf + i, needle, nl) == 0) {
            return 1;
        }
    }
    return 0;
}

static int32_t case_handle(void) {
    const char *body;
    if (pm_metal_inspect_handle("GET", "/health") != 200) {
        return fail("health status");
    }
    body = pm_metal_inspect_body();
    if (body == NULL || strstr(body, "\"ok\":true") == NULL) {
        return fail("health body");
    }
    if (pm_metal_inspect_handle("GET", "/inspect/self") != 200) {
        return fail("self status");
    }
    body = pm_metal_inspect_body();
    if (body == NULL || strstr(body, "\"name\":\"pymergetic.metal\"") == NULL) {
        return fail("self body");
    }
    if (pm_metal_inspect_handle("GET", "/inspect/reg") != 200) {
        return fail("reg status");
    }
    body = pm_metal_inspect_body();
    if (body == NULL || strstr(body, "pymergetic.metal") == NULL) {
        return fail("reg body");
    }
    if (pm_metal_inspect_handle("GET", "/inspect/reg/completeness?fmt=tree") != 200) {
        return fail("tree status");
    }
    body = pm_metal_inspect_body();
    if (body == NULL || strstr(body, "registry") == NULL || strstr(body, "+-- ") == NULL) {
        return fail("tree body");
    }
    if (pm_metal_inspect_handle("GET", "/nope") != 404) {
        return fail("404");
    }
    /* /build/<fqn>: unbuilt card is 404 — a record exists only after a
     * runtime unit_compile, and this test has not built one. */
    if (pm_metal_inspect_handle("GET", "/build/pymergetic.metal.jit.c") != 404) {
        return fail("build unbuilt 404");
    }
    if (pm_metal_inspect_handle("GET", "/build/") != 404) {
        return fail("build empty 404");
    }
    if (pm_metal_inspect_handle("GET", "/capabilities") != 200) {
        return fail("caps status");
    }
    body = pm_metal_inspect_body();
    if (body == NULL || strstr(body, "\"asgi\":true") == NULL
        || strstr(body, "\"microdot\":true") == NULL || strstr(body, "\"zenoh\":true") == NULL) {
        return fail("caps body");
    }
    return 0;
}

static int32_t case_http(void) {
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st;
    if (pm_metal_net_http_asgi_listen(LO4, INSPECT_PORT) != 0) {
        return fail("listen");
    }
    pm_metal_async_poll();
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/inspect/self", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch self");
    }
    if (body == NULL || n == 0 || !has(body, n, "pymergetic.metal")) {
        return fail("fetch self body");
    }
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/inspect/reg", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch reg");
    }
    if (body == NULL || n == 0 || !has(body, n, "live_registry")) {
        return fail("fetch reg body");
    }
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/inspect/reg/pymergetic.metal.inspect",
        &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch reg module");
    }
    if (body == NULL || n == 0 || !has(body, n, "\"export_count\"")
        || !has(body, n, "pm_metal_inspect_handle")) {
        return fail("fetch reg module body");
    }
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch(
        "http://127.0.0.1:8090/inspect/reg/pymergetic.metal.inspect/pm_metal_inspect_handle",
        &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch reg export");
    }
    if (body == NULL || n == 0 || !has(body, n, "\"found\":true")
        || !has(body, n, "\"kind\":\"fn\"")) {
        return fail("fetch reg export body");
    }
    /* RPC on a resident (native C) module must be refused, not crash. */
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch(
        "http://127.0.0.1:8090/inspect/call/pymergetic.wasmmod.registry/pm_wasmmod_registry_module_count",
        &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch rpc resident");
    }
    if (body == NULL || n == 0 || !has(body, n, "\"error\":\"native_module\"")) {
        return fail("fetch rpc resident body");
    }
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/inspect/index.html", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch www");
    }
    if (body == NULL || n == 0 || !has(body, n, "<title>Inspect</title>")) {
        return fail("fetch www body");
    }
    /* Card source over HTTP: the /src/<fqn> manifest and /src/<fqn>/<file>
     * route serve the embedded C/Rust (the same bytes the commander's source
     * pane shows on every seat). */
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch(
        "http://127.0.0.1:8090/src/pymergetic.metal.inspect", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch src manifest");
    }
    if (body == NULL || n == 0 || !has(body, n, "\"name\":\"pymergetic.metal.inspect\"")
        || !has(body, n, "\"files\"")) {
        return fail("fetch src manifest body");
    }
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch(
        "http://127.0.0.1:8090/src/pymergetic.metal.inspect/__impl__.c", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch src raw");
    }
    if (body == NULL || n == 0 || !has(body, n, "pm_metal_inspect_init")) {
        return fail("fetch src raw body");
    }
    /* An unknown fqn/file must not serve the real card (route miss → empty
     * reply), never a crash or the wrong card's bytes. */
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch(
        "http://127.0.0.1:8090/src/no.such.card/nope.c", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch src missing");
    }
    if (body != NULL && n != 0 && has(body, n, "pm_metal_inspect_init")) {
        return fail("fetch src missing body served real source");
    }
    return 0;
}

int32_t pm_metal_inspect_tests(void) {
    if (case_handle() != 0) {
        return 1;
    }
    if (case_http() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.inspect, tests, pm_metal_inspect_tests);
