/* pymergetic.metal.net.http.asgi — parked io.fetch to RS listen on lo. */
#define _GNU_SOURCE
#include "pymergetic/metal/coop.h"
#include "pymergetic/util/limits.h"
#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/http/asgi.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define ASGI_PORT 8090
/* Servers this case starts of its own, clear of 8090/8091 above. */
#define KNOB_PORT 8100

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.http.asgi test: %s\n", why);
    return 1;
}

static int32_t case_fetch_default(void) {
    /* CoroHead: the coro prefix embedded at the head of every asgi frame —
     * drift here means the embedded-frame layouts (conn coro first) break.
     * 6 words: step, awaiting, waiter, task, status, vm_only+auto_free pad. */
    _Static_assert(sizeof(pm_metal_coop_coro_t) == 48, "asgi CoroHead");
    if (pm_metal_net_http_asgi_listen(LO4, ASGI_PORT) != 0) {
        return fail("listen");
    }
    pm_metal_coop_poll();
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st = pm_metal_net_http_fetch("http://127.0.0.1:8090/x", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch");
    }
    if (n != 4 || body == NULL || memcmp(body, "asgi", 4) != 0) {
        return fail("body");
    }
    return 0;
}

static int32_t fn_hello(const char *method, const char *path, uint8_t *out, uint32_t out_max,
    uint32_t *out_len) {
    const char *s = "fn-ok";
    uint32_t n = 5;
    (void)method;
    (void)path;
    if (out == NULL || out_len == NULL || out_max < n) {
        return -1;
    }
    memcpy(out, s, n);
    *out_len = n;
    return 0;
}

static int32_t case_route(void) {
    const uint8_t hello[] = "hello";
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st;
    if (pm_metal_net_http_asgi_route("GET", "/hi", hello, 5) != 0) {
        return fail("route");
    }
    pm_metal_coop_poll();
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/hi", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch route");
    }
    if (n != 5 || body == NULL || memcmp(body, "hello", 5) != 0) {
        return fail("route body");
    }
    if (pm_metal_net_http_asgi_route_fn("GET", "/fn", fn_hello) != 0) {
        return fail("route_fn");
    }
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/fn", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch route_fn");
    }
    if (n != 5 || body == NULL || memcmp(body, "fn-ok", 5) != 0) {
        return fail("route_fn body");
    }
    {
        static const uint8_t page[] = "<h1>static</h1>";
        if (pm_metal_net_http_asgi_route_static("/s.html", page, 15) != 0) {
            return fail("route_static");
        }
        body = NULL;
        n = 0;
        st = pm_metal_net_http_fetch("http://127.0.0.1:8090/s.html", &body, &n, err, sizeof(err));
        if (st != PM_WASMMOD_IO_OK) {
            return fail(err[0] ? err : "fetch route_static");
        }
        if (n != 15 || body == NULL || memcmp(body, page, 15) != 0) {
            return fail("route_static body");
        }
    }
    return 0;
}

/* Big dynamic response: a route_fn that emits well past the old 16 KiB wall.
 * On this platform the body budget is 1 MiB by default, so a 64 KiB handler
 * must round-trip intact. Returns an internal offset so the caller can verify
 * the body's synthetic pattern (byte i == i mod 251). */
#define BIG_N 65536u
static int32_t fn_big(const char *method, const char *path, uint8_t *out, uint32_t out_max,
    uint32_t *out_len) {
    (void)method;
    (void)path;
    if (out == NULL || out_len == NULL || out_max < BIG_N) {
        return -1;
    }
    for (uint32_t i = 0u; i < BIG_N; ++i) {
        out[i] = (uint8_t)(i % 251u);
    }
    *out_len = BIG_N;
    return 0;
}

static int32_t case_big_body(void) {
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64] = {0};
    pm_wasmmod_io_result_t st;
    if (pm_metal_net_http_asgi_route_fn("GET", "/big", fn_big) != 0) {
        return fail("route_fn big");
    }
    pm_metal_coop_poll();
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/big", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch big");
    }
    if (n != BIG_N || body == NULL) {
        return fail("big length");
    }
    for (uint32_t i = 0u; i < BIG_N; ++i) {
        if (body[i] != (uint8_t)(i % 251u)) {
            return fail("big pattern");
        }
    }
    return 0;
}

/* Streaming download: a resident producer that feeds a 300 KiB body in 16 KiB
 * chunks, with a declared size. The server never buffers the whole body; the
 * fetch must receive all 300 KiB intact. */
#define STREAM_N 307200u /* 300 KiB, sizes the body far beyond one chunk */
typedef struct shake_ctx {
    uint32_t sent;
    int32_t calls;
    int32_t fail_after;
} shake_ctx_t;

static uint64_t shake_size(void *vctx) {
    (void)vctx;
    return STREAM_N;
}

static int32_t shake_next(void *vctx, uint8_t *chunk, uint32_t *len, uint32_t cap, int32_t *more) {
    shake_ctx_t *x = (shake_ctx_t *)vctx;
    if (cap == 0 || len == NULL || more == NULL) {
        return -1;
    }
    if (x->fail_after > 0 && x->calls >= x->fail_after) {
        return -1;
    }
    uint32_t remain = STREAM_N - x->sent;
    uint32_t take = remain < cap ? remain : cap;
    for (uint32_t i = 0u; i < take; ++i) {
        chunk[i] = (uint8_t)((x->sent + i) % 251u);
    }
    *len = take;
    x->sent += take;
    x->calls += 1;
    *more = (x->sent < STREAM_N) ? 1 : 0;
    return 0;
}

static int32_t case_stream(void) {
    static shake_ctx_t ctx = {0, 0, 0};
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64] = {0};
    pm_wasmmod_io_result_t st;
    ctx.sent = 0;
    ctx.calls = 0;
    ctx.fail_after = 0;
    if (pm_metal_net_http_asgi_route_stream_fn("GET", "/dl", &ctx, shake_size, shake_next) != 0) {
        return fail("route_stream");
    }
    pm_metal_coop_poll();
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/dl", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch stream");
    }
    if (n != STREAM_N || body == NULL) {
        return fail("stream length");
    }
    if (ctx.calls < 2) {
        return fail("stream not chunked");
    }
    for (uint32_t i = 0u; i < STREAM_N; ++i) {
        if (body[i] != (uint8_t)(i % 251u)) {
            return fail("stream pattern");
        }
    }
    return 0;
}

static int32_t case_multi_instance(void) {
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st;
    int32_t a = pm_metal_net_http_asgi_listen(LO4, ASGI_PORT); /* id 0 dup-safe */
    if (a != 0 || pm_metal_net_http_asgi_status(0) != 1) {
        return fail("dup listen");
    }
    if (pm_metal_net_http_asgi_count() < 1) {
        return fail("count");
    }
    int32_t b = pm_metal_net_http_asgi_listen(LO4, ASGI_PORT + 1); /* new instance */
    if (b <= a || pm_metal_net_http_asgi_status(b) != 1) {
        return fail("listen 2nd");
    }
    pm_metal_coop_poll();
    st = pm_metal_net_http_fetch("http://127.0.0.1:8091/x", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK || n != 4 || body == NULL || memcmp(body, "asgi", 4) != 0) {
        return fail("fetch 2nd");
    }
    if (pm_metal_net_http_asgi_stop(b) != 0 || pm_metal_net_http_asgi_status(b) != 0) {
        return fail("stop 2nd");
    }
    if (pm_metal_net_http_asgi_status(0) != 1) {
        return fail("stop clobbered first");
    }
    return 0;
}

/* The seat index: GET / serves the inspection landing (also served by the
 * CDN's shared www design), never the raw asgi octet-stream fallback. The
 * inspect card registers "/" at boot via inspect_www_mount(); /x stays asgi. */
static int32_t case_fetch_root(void) {
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st = pm_metal_net_http_fetch("http://127.0.0.1:8090/", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "fetch root");
    }
    if (n < 100 || body == NULL || memmem(body, n, "pymergetic.metal", 16) == NULL) {
        return fail("root landing");
    }
    if (n == 4 && memcmp(body, "asgi", 4) == 0) {
        return fail("root is octet-stream fallback");
    }
    /* Unmounted paths still fall back to the asgi default. */
    body = NULL;
    n = 0;
    st = pm_metal_net_http_fetch("http://127.0.0.1:8090/x", &body, &n, err, sizeof(err));
    if (st != PM_WASMMOD_IO_OK || n != 4 || body == NULL || memcmp(body, "asgi", 4) != 0) {
        return fail("fallback changed");
    }
    return 0;
}

/* Raw GET so the response *headers* are visible — io.fetch hands back only the
 * body, and the declared Content-Type is what this case is about. */
static int32_t raw_get(const char *path, uint8_t *out, uint32_t out_max, uint32_t *out_len) {
    char req[160];
    int rn = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n", path);
    int32_t fd = pm_metal_net_ip_out_socket(PM_METAL_NET_IP_SOCK_STREAM);
    uint32_t got = 0;
    int i;
    if (fd < 0 || rn <= 0) {
        return -1;
    }
    if (pm_metal_net_ip_connect(fd, LO4, ASGI_PORT) < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    for (i = 0; i < 400; i++) {
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
        if (pm_metal_net_ip_send(fd, (const uint8_t *)req, (uint32_t)rn) == rn) {
            break;
        }
    }
    for (i = 0; i < 4000 && got < out_max; i++) {
        int32_t k = pm_metal_net_ip_recv(fd, out + got, out_max - got);
        if (k > 0) {
            got += (uint32_t)k;
            continue;
        }
        if (got != 0 && memmem(out, got, "\r\n\r\n", 4) != NULL) {
            break;
        }
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
    }
    (void)pm_metal_net_ip_close(fd);
    *out_len = got;
    return got != 0 ? 0 : -1;
}

static int32_t case_route_ctype(void) {
    static uint8_t rsp[2048];
    uint32_t n = 0;
    /* The path ends in a dotted segment on purpose: extension sniffing would
     * read ".c" and answer octet-stream, which is what made a browser download
     * /inspect/reg/<fqn> instead of showing it. */
    if (pm_metal_net_http_asgi_route_fn_ct("GET", "/j/a.b.c", fn_hello, "application/json") != 0) {
        return fail("route_fn_ct");
    }
    if (pm_metal_net_http_asgi_route_fn_ct("GET", "/j/null", fn_hello, NULL) == 0) {
        return fail("route_fn_ct took a null type");
    }
    if (raw_get("/j/a.b.c", rsp, sizeof(rsp), &n) != 0) {
        return fail("raw get");
    }
    if (memmem(rsp, n, "Content-Type: application/json", 29) == NULL) {
        return fail("declared ctype missing");
    }
    if (memmem(rsp, n, "fn-ok", 5) == NULL) {
        return fail("ctype route body");
    }
    /* Static bytes at an extension-less URL (a mounted directory index) must
     * carry the declared type too, or a browser downloads the page. */
    static const uint8_t page[] = "<!doctype html><p>dir";
    if (pm_metal_net_http_asgi_route_static_ct("/dir", page, (uint32_t)(sizeof(page) - 1),
            "text/html; charset=utf-8") != 0) {
        return fail("route_static_ct");
    }
    if (pm_metal_net_http_asgi_route_static_ct("/dir2", page, 1, NULL) == 0) {
        return fail("route_static_ct took a null type");
    }
    n = 0;
    if (raw_get("/dir", rsp, sizeof(rsp), &n) != 0) {
        return fail("raw get dir");
    }
    if (memmem(rsp, n, "Content-Type: text/html", 23) == NULL) {
        return fail("static ctype missing");
    }
    /* An undeclared route still derives its type from the path extension —
     * nothing is mounted here, the path only has to carry one. */
    n = 0;
    if (raw_get("/no/such/file.css", rsp, sizeof(rsp), &n) != 0) {
        return fail("raw get css");
    }
    if (memmem(rsp, n, "Content-Type: text/css", 22) == NULL) {
        return fail("extension ctype regressed");
    }
    return 0;
}

/* Deferred route: the body comes from a renderer that is not a C callback. The
 * seat's renderer is MicroPython (it owns the templates); here the prove plays
 * that part in C, so the queue/park/wake mechanism is proved without a Python
 * runtime. Same card face either way — only the renderer differs. */
static int32_t case_defer(void) {
    static uint8_t rsp[4096];
    uint32_t n = 0;
    const char *want = "<h1>rendered elsewhere</h1>";
    if (pm_metal_net_http_asgi_route_defer("/packs/*", "text/html; charset=utf-8") != 0) {
        return fail("route_defer");
    }
    if (pm_metal_net_http_asgi_route_defer(NULL, "text/html") == 0
        || pm_metal_net_http_asgi_route_defer("/x", NULL) == 0) {
        return fail("route_defer took a null");
    }
    /* Nothing is pending before a request arrives. */
    if (pm_metal_net_http_asgi_defer_next() != NULL) {
        return fail("defer_next invented work");
    }
    if (pm_metal_net_http_asgi_defer_reply((const uint8_t *)want, 4) == 0) {
        return fail("defer_reply without a current request");
    }
    /* Drive the request and the renderer together: the connection parks in the
     * async poll while this loop plays the render pump. */
    {
        int32_t fd = pm_metal_net_ip_out_socket(PM_METAL_NET_IP_SOCK_STREAM);
        const char *req = "GET /packs/pymergetic.metal.net.ip HTTP/1.0\r\nHost: x\r\n\r\n";
        uint32_t rn = (uint32_t)strlen(req);
        int served = 0;
        int i;
        if (fd < 0 || pm_metal_net_ip_connect(fd, LO4, ASGI_PORT) < 0) {
            return fail("defer connect");
        }
        for (i = 0; i < 400; i++) {
            pm_metal_net_ip_pump();
            pm_metal_coop_poll();
            if (pm_metal_net_ip_send(fd, (const uint8_t *)req, rn) == (int32_t)rn) {
                break;
            }
        }
        for (i = 0; i < 4000 && n < sizeof(rsp); i++) {
            if (!served) {
                const char *path = (const char *)pm_metal_net_http_asgi_defer_next();
                if (path != NULL) {
                    /* The renderer sees the whole path, so it can pick a page. */
                    if (strcmp(path, "/packs/pymergetic.metal.net.ip") != 0) {
                        (void)pm_metal_net_ip_close(fd);
                        return fail("defer path");
                    }
                    if (pm_metal_net_http_asgi_defer_reply((const uint8_t *)want,
                            (uint32_t)strlen(want)) != 0) {
                        (void)pm_metal_net_ip_close(fd);
                        return fail("defer_reply");
                    }
                    served = 1;
                }
            }
            int32_t k = pm_metal_net_ip_recv(fd, rsp + n, (uint32_t)sizeof(rsp) - n);
            if (k > 0) {
                n += (uint32_t)k;
                continue;
            }
            /* Header and body arrive in separate sends, so wait for the body
             * itself rather than stopping at the first blank line. */
            if (served && n != 0 && memmem(rsp, n, want, strlen(want)) != NULL) {
                break;
            }
            pm_metal_net_ip_pump();
            pm_metal_coop_poll();
        }
        (void)pm_metal_net_ip_close(fd);
        if (!served) {
            return fail("renderer never saw the request");
        }
    }
    if (memmem(rsp, n, want, strlen(want)) == NULL) {
        return fail("deferred body");
    }
    if (memmem(rsp, n, "Content-Type: text/html", 23) == NULL) {
        return fail("deferred ctype");
    }
    /* Content-Length must reflect the rendered body, not a guess made before it
     * existed — that is why the header waits for the reply. */
    if (memmem(rsp, n, "Content-Length: 27", 18) == NULL) {
        return fail("deferred content-length");
    }
    return 0;
}

/* Two-request /packs serve prove: a first deferred request is answered, then a
 * second, already-parked (concurrent) request is answered after it — the exact
 * "first package loads, then the next one freezes" failure from the old
 * single-VM-owner model. Two clients park on the queue; the render pump must
 * hand out first one path then the other, each with its own body, and neither
 * connection may be stranded when the next defer_next comes along. */
static int32_t case_defer_two(void) {
    enum { K = 2 };
    int32_t fd[K];
    const char *paths[K] = { "/packs/alpha", "/packs/beta" };
    const char *body[K] = { "<h1>alpha</h1>", "<h1>beta</h1>" };
    uint32_t rn[K];
    char req[K][160];
    uint8_t rsp[K][1024];
    uint32_t got[K] = { 0, 0 };
    const char *reqfmt = "GET %s HTTP/1.0\r\nHost: x\r\n\r\n";
    int saw[K] = { 0, 0 };   /* which path the pump drained, in order */
    int i;
    int spin;
    if (pm_metal_net_http_asgi_route_defer("/packs/*", "text/html; charset=utf-8") != 0) {
        return fail("defer_two route");
    }
    for (i = 0; i < K; i++) {
        fd[i] = pm_metal_net_ip_out_socket(PM_METAL_NET_IP_SOCK_STREAM);
        if (fd[i] < 0 || pm_metal_net_ip_connect(fd[i], LO4, ASGI_PORT) < 0) {
            return fail("defer_two connect");
        }
        rn[i] = (uint32_t)snprintf(req[i], sizeof(req[i]), reqfmt, paths[i]);
    }
    /* Send both requests and let both connections park on the queue. */
    for (spin = 0; spin < 800; spin++) {
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
        int all_sent = 1;
        for (i = 0; i < K; i++) {
            if (pm_metal_net_ip_send(fd[i], (const uint8_t *)req[i], rn[i]) != (int32_t)rn[i]) {
                all_sent = 0;
            }
        }
        if (all_sent) {
            break;
        }
    }
    /* Let both requests arrive and park before the pump drains anything. We
     * cannot count the deferred queue from the test side, so give the two
     * connections a fixed window to enqueue. */
    for (spin = 0; spin < 400; spin++) {
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
    }
    /* Play the render pump: drain path[0] first, then path[1]. defer_next may
     * return either path first since two park concurrently, so accept either
     * order but require that BOTH arrive and both get their own body. */
    for (spin = 0; spin < 4000 && !(saw[0] && saw[1]); spin++) {
        const char *p = (const char *)pm_metal_net_http_asgi_defer_next();
        if (p == NULL) {
            pm_metal_net_ip_pump();
            pm_metal_coop_poll();
            continue;
        }
        int matched = -1;
        for (i = 0; i < K; i++) {
            if (strcmp(p, paths[i]) == 0) {
                matched = i;
                break;
            }
        }
        if (matched < 0) {
            return fail("defer_two unexpected path");
        }
        saw[matched] = 1;
        if (pm_metal_net_http_asgi_defer_reply(
                (const uint8_t *)body[matched], (uint32_t)strlen(body[matched])) != 0) {
            return fail("defer_two reply");
        }
    }
    for (i = 0; i < K; i++) {
        for (spin = 0; spin < 2000 && got[i] < sizeof(rsp[i]) - 1; spin++) {
            int32_t k = pm_metal_net_ip_recv(fd[i], rsp[i] + got[i],
                (uint32_t)sizeof(rsp[i]) - 1 - got[i]);
            if (k > 0) {
                got[i] += (uint32_t)k;
                continue;
            }
            if (got[i] != 0 && memmem(rsp[i], got[i], body[i], strlen(body[i])) != NULL) {
                break;
            }
            pm_metal_net_ip_pump();
            pm_metal_coop_poll();
        }
        (void)pm_metal_net_ip_close(fd[i]);
        if (!saw[i]) {
            return fail("defer_two renderer missed a path");
        }
        if (memmem(rsp[i], got[i], body[i], strlen(body[i])) == NULL) {
            return fail("defer_two body");
        }
        /* The Content-Length must be per-path, not a stale value from the
         * previous drain — a shared-global renderer bug off by one page would
         * break the concurrent second request. */
        char cl[24];
        int cln = snprintf(cl, sizeof(cl), "Content-Length: %u",
            (unsigned)strlen(body[i]));
        if (memmem(rsp[i], got[i], cl, (size_t)cln) == NULL) {
            return fail("defer_two content-length");
        }
    }
    return 0;
}

/* A burst of deferred requests must not cost the server its connection slots.
 * More clients than MAX_CONN is normal load, and a renderer that is slow (or
 * absent for a moment) must leave the httpd answering afterwards — a leaked slot
 * is permanent, so this is the difference between a slow seat and a dead one. */
static int32_t case_defer_burst(void) {
    enum { BURST = 8 };
    int32_t fd[BURST];
    const char *req = "GET /packs/burst HTTP/1.0\r\nHost: x\r\n\r\n";
    uint32_t rn = (uint32_t)strlen(req);
    const char *body = "<h1>burst</h1>";
    int i;
    int spin;
    /* Open every client first, so the queue and the slots are both contended. */
    for (i = 0; i < BURST; i++) {
        fd[i] = pm_metal_net_ip_out_socket(PM_METAL_NET_IP_SOCK_STREAM);
        if (fd[i] >= 0) {
            (void)pm_metal_net_ip_connect(fd[i], LO4, ASGI_PORT);
        }
    }
    for (spin = 0; spin < 600; spin++) {
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
        for (i = 0; i < BURST; i++) {
            if (fd[i] >= 0) {
                (void)pm_metal_net_ip_send(fd[i], (const uint8_t *)req, rn);
            }
        }
    }
    /* Now play the renderer, deliberately behind the clients. */
    for (spin = 0; spin < 8000; spin++) {
        const char *path = (const char *)pm_metal_net_http_asgi_defer_next();
        if (path != NULL) {
            (void)pm_metal_net_http_asgi_defer_reply((const uint8_t *)body,
                (uint32_t)strlen(body));
        }
        for (i = 0; i < BURST; i++) {
            uint8_t sink[512];
            if (fd[i] >= 0) {
                (void)pm_metal_net_ip_recv(fd[i], sink, (uint32_t)sizeof(sink));
            }
        }
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
    }
    for (i = 0; i < BURST; i++) {
        if (fd[i] >= 0) {
            (void)pm_metal_net_ip_close(fd[i]);
        }
    }
    for (spin = 0; spin < 2000; spin++) {
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
    }
    /* The whole point: the server is still there. */
    {
        static uint8_t rsp[2048];
        uint32_t n = 0;
        int32_t c = pm_metal_net_ip_out_socket(PM_METAL_NET_IP_SOCK_STREAM);
        const char *g = "GET /health HTTP/1.0\r\nHost: x\r\n\r\n";
        uint32_t gn = (uint32_t)strlen(g);
        if (c < 0 || pm_metal_net_ip_connect(c, LO4, ASGI_PORT) < 0) {
            return fail("post-burst connect");
        }
        for (spin = 0; spin < 4000 && n < sizeof(rsp); spin++) {
            (void)pm_metal_net_ip_send(c, (const uint8_t *)g, gn);
            int32_t k = pm_metal_net_ip_recv(c, rsp + n, (uint32_t)sizeof(rsp) - n);
            if (k > 0) {
                n += (uint32_t)k;
            }
            if (n != 0 && memmem(rsp, n, "\r\n\r\n", 4) != NULL) {
                break;
            }
            pm_metal_net_ip_pump();
            pm_metal_coop_poll();
        }
        (void)pm_metal_net_ip_close(c);
        if (memmem(rsp, n, "200 OK", 6) == NULL) {
            return fail("server wedged after a deferred burst");
        }
    }
    return 0;
}

/* The four knobs this card grows against. Nothing above may depend on the
 * numbers this card shipped with: a seat that expects more traffic says so at
 * runtime, and a seat that will not be asked for more keeps the memory. So
 * this case starts more servers than the shipped table holds, and is refused
 * the one the knob does not allow. */
static int32_t case_server_knob(void) {
    enum { EXTRA = 9 }; /* past the shipped 8, so the table has to grow */
    int32_t ids[EXTRA];
    uint32_t live = pm_metal_net_http_asgi_count();
    int32_t slot = pm_util_limits_find("pymergetic.metal.net.http.asgi.server");
    int i;
    if (slot < 0) {
        return fail("the server knob is not on this seat");
    }
    if (pm_util_limits_default(slot) != 8u) {
        return fail("asgi shipped with another server default");
    }
    if (pm_util_limits_used(slot) != live) {
        return fail("the server knob lost count of the servers");
    }
    {
        int32_t c = pm_util_limits_find("pymergetic.metal.net.http.asgi.connection");
        int32_t d = pm_util_limits_find("pymergetic.metal.net.http.asgi.defer");
        int32_t b = pm_util_limits_find("pymergetic.metal.net.http.asgi.backlog");
        if (c < 0 || d < 0 || b < 0) {
            return fail("an asgi knob is missing");
        }
        if (pm_util_limits_default(c) != 16u || pm_util_limits_default(d) != 16u
            || pm_util_limits_default(b) != 12u) {
            return fail("asgi shipped with other defaults");
        }
    }
    /* Room for exactly one more than there are now. */
    if (pm_util_limits_set("pymergetic.metal.net.http.asgi.server", live + 1u) != 0) {
        return fail("set the server knob");
    }
    {
        int32_t one = pm_metal_net_http_asgi_listen(LO4, KNOB_PORT);
        if (one < 0) {
            return fail("the one allowed server did not start");
        }
        if (pm_metal_net_http_asgi_listen(LO4, KNOB_PORT + 1) >= 0) {
            return fail("a server started past its knob");
        }
        if (pm_metal_net_http_asgi_stop(one) != 0) {
            return fail("stop the allowed server");
        }
    }
    /* Now more than the shipped table holds. */
    if (pm_util_limits_set("pymergetic.metal.net.http.asgi.server", live + (uint32_t)EXTRA) != 0) {
        return fail("raise the server knob");
    }
    for (i = 0; i < EXTRA; i++) {
        ids[i] = pm_metal_net_http_asgi_listen(LO4, (uint16_t)(KNOB_PORT + i));
        if (ids[i] < 0) {
            return fail("raising the knob did not open the seat");
        }
    }
    if (pm_util_limits_used(slot) != live + (uint32_t)EXTRA) {
        return fail("the server knob miscounted a grown table");
    }
    /* The last one — the one only a grown table could hold — must serve. */
    {
        uint8_t *body = NULL;
        uint32_t n = 0;
        char url[64];
        char err[64];
        pm_wasmmod_io_result_t st;
        (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/x", KNOB_PORT + EXTRA - 1);
        pm_metal_coop_poll();
        st = pm_metal_net_http_fetch(url, &body, &n, err, sizeof(err));
        if (st != PM_WASMMOD_IO_OK || n != 4 || body == NULL || memcmp(body, "asgi", 4) != 0) {
            return fail("the grown server does not answer");
        }
    }
    for (i = 0; i < EXTRA; i++) {
        if (pm_metal_net_http_asgi_stop(ids[i]) != 0) {
            return fail("stop a grown server");
        }
    }
    if (pm_util_limits_used(slot) != live) {
        return fail("stopped servers kept their slot");
    }
    if (pm_util_limits_reset("pymergetic.metal.net.http.asgi.server") != 0) {
        return fail("reset the server knob");
    }
    return 0;
}

/* More clients at once than the shipped connection table holds, all answered,
 * and every one of them handed its memory back. The old table was sixteen
 * connections of .bss whether this seat served a request or not; this one is
 * as wide as the traffic and as narrow as idle.
 *
 * The listener for this case is started after the knobs are moved, because a
 * backlog is asked for once at listen(): twenty clients arriving together
 * need somewhere to wait, and that is the backlog knob's whole job. */
static int32_t case_conn_knob(void) {
    enum { CLIENTS = 20, BURST_PORT = KNOB_PORT + 20 };
    int32_t fd[CLIENTS];
    uint32_t got[CLIENTS];
    static uint8_t rsp[CLIENTS][256];
    const char *req = "GET /x HTTP/1.0\r\nHost: x\r\n\r\n";
    uint32_t rn = (uint32_t)strlen(req);
    int32_t slot = pm_util_limits_find("pymergetic.metal.net.http.asgi.connection");
    int32_t srv;
    int i;
    int spin;
    int answered = 0;
    if (slot < 0) {
        return fail("the connection knob is not on this seat");
    }
    if (pm_util_limits_used(slot) != 0u) {
        return fail("a connection was still held before the burst");
    }
    /* pymergetic.metal.net.ip.backlog is the ceiling under which the asgi one asks: the queue
     * is the ip card's memory, so both have to be willing. */
    if (pm_util_limits_set("pymergetic.metal.net.http.asgi.connection", 32u) != 0
        || pm_util_limits_set("pymergetic.metal.net.http.asgi.backlog", 32u) != 0
        || pm_util_limits_set("pymergetic.metal.net.ip.backlog", 32u) != 0
        || pm_util_limits_set("pymergetic.metal.net.ip.socket", 96u) != 0) {
        return fail("raise the knobs for the burst");
    }
    srv = pm_metal_net_http_asgi_listen(LO4, BURST_PORT);
    if (srv < 0) {
        return fail("the burst server did not start");
    }
    for (i = 0; i < CLIENTS; i++) {
        got[i] = 0;
        fd[i] = pm_metal_net_ip_out_socket(PM_METAL_NET_IP_SOCK_STREAM);
        if (fd[i] < 0) {
            return fail("out of client sockets");
        }
        if (pm_metal_net_ip_connect(fd[i], LO4, BURST_PORT) < 0) {
            return fail("client connect");
        }
    }
    for (spin = 0; spin < 8000 && answered < CLIENTS; spin++) {
        answered = 0;
        for (i = 0; i < CLIENTS; i++) {
            if (got[i] == 0) {
                (void)pm_metal_net_ip_send(fd[i], (const uint8_t *)req, rn);
            }
            if (got[i] < sizeof(rsp[i])) {
                int32_t k = pm_metal_net_ip_recv(fd[i], rsp[i] + got[i],
                    (uint32_t)sizeof(rsp[i]) - got[i]);
                if (k > 0) {
                    got[i] += (uint32_t)k;
                }
            }
            if (got[i] != 0 && memmem(rsp[i], got[i], "200 OK", 6) != NULL) {
                answered++;
            }
        }
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
    }
    for (i = 0; i < CLIENTS; i++) {
        (void)pm_metal_net_ip_close(fd[i]);
    }
    for (spin = 0; spin < 3000; spin++) {
        pm_metal_net_ip_pump();
        pm_metal_coop_poll();
    }
    if (pm_metal_net_http_asgi_stop(srv) != 0) {
        return fail("stop the burst server");
    }
    if (answered != CLIENTS) {
        fprintf(stderr, "asgi burst: answered %d of %d, %u connection(s) still held\n",
            answered, (int)CLIENTS, pm_util_limits_used(slot));
        return fail("the raised connection knob did not serve every client");
    }
    if (pm_util_limits_used(slot) != 0u) {
        return fail("finished connections did not go back to the arena");
    }
    if (pm_util_limits_reset("pymergetic.metal.net.http.asgi.connection") != 0
        || pm_util_limits_reset("pymergetic.metal.net.http.asgi.backlog") != 0
        || pm_util_limits_reset("pymergetic.metal.net.ip.backlog") != 0
        || pm_util_limits_reset("pymergetic.metal.net.ip.socket") != 0) {
        return fail("reset the knobs after the burst");
    }
    return 0;
}

int32_t pm_metal_net_http_asgi_tests(void) {
    if (case_fetch_default() != 0) {
        return 1;
    }
    if (case_fetch_root() != 0) {
        return 1;
    }
    if (case_route() != 0) {
        return 1;
    }
    if (case_route_ctype() != 0) {
        return 1;
    }
    if (case_defer() != 0) {
        return 1;
    }
    if (case_defer_two() != 0) {
        return 1;
    }
    if (case_defer_burst() != 0) {
        return 1;
    }
    if (case_big_body() != 0) {
        return 1;
    }
    if (case_stream() != 0) {
        return 1;
    }
    if (case_multi_instance() != 0) {
        return 1;
    }
    if (case_server_knob() != 0) {
        return 1;
    }
    if (case_conn_knob() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.http.asgi, tests, pm_metal_net_http_asgi_tests);
