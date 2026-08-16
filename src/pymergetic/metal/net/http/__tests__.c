/* pymergetic.metal.net.http — parked io.fetch on lo and sim L2. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/drivers/net/sim.h"
#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/tls.h"
#include "pymergetic/metal/net/tls/__testcert__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/net/cdn.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define IF4 0x0a000001u
#define HTTP_PORT 8080
#define HTTPS_PORT 8444
#define AUTH_PORT 8082
#define SIM_HTTP_PORT 8088

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    int32_t ls;
    int32_t acc;
    pm_metal_net_tls_session_t *tls;
} http_srv_t;

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.http test: %s\n", why);
    return 1;
}

static const uint8_t k_resp[] =
    "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello";

static pm_metal_async_status_t step_server(pm_metal_async_coro_t *self) {
    http_srv_t *f = (http_srv_t *)self;
    if (f->step == 0) {
        int32_t a = pm_metal_net_ip_accept(f->ls);
        if (a == -2) {
            return PM_METAL_ASYNC_WAITING;
        }
        if (a < 0) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->acc = a;
        f->step = 1;
    }
    uint8_t buf[128];
    int32_t n = pm_metal_net_ip_recv(f->acc, buf, sizeof(buf));
    if (n == 0) {
        return PM_METAL_ASYNC_WAITING;
    }
    if (n < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (pm_metal_net_ip_send(f->acc, k_resp, sizeof(k_resp) - 1u) < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    (void)pm_metal_net_ip_close(f->acc);
    return PM_METAL_ASYNC_DONE;
}

static pm_metal_async_status_t step_https_server(pm_metal_async_coro_t *self) {
    http_srv_t *f = (http_srv_t *)self;
    if (f->step == 0) {
        int32_t a = pm_metal_net_ip_accept(f->ls);
        if (a == -2) {
            return PM_METAL_ASYNC_WAITING;
        }
        if (a < 0) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->acc = a;
        f->tls = pm_metal_net_tls_server(a, pm_metal_net_tls_test_cert, PM_METAL_NET_TLS_TEST_CERT_LEN,
            pm_metal_net_tls_test_key, PM_METAL_NET_TLS_TEST_KEY_LEN);
        if (f->tls == NULL) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->step = 1;
    }
    if (f->step == 1) {
        int32_t st = pm_metal_net_tls_handshake(f->tls);
        if (st == 1) {
            return PM_METAL_ASYNC_WAITING;
        }
        if (st < 0) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->step = 2;
    }
    uint8_t buf[128];
    int32_t n = pm_metal_net_tls_recv(f->tls, buf, sizeof(buf));
    if (n == 0) {
        return PM_METAL_ASYNC_WAITING;
    }
    if (n < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (pm_metal_net_tls_send(f->tls, k_resp, sizeof(k_resp) - 1u) < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    pm_metal_net_tls_close(f->tls);
    f->tls = NULL;
    (void)pm_metal_net_ip_close(f->acc);
    return PM_METAL_ASYNC_DONE;
}

static int32_t serve_and_fetch(uint32_t addr, uint16_t port, int https, pm_metal_async_step_fn srv_step,
    const char *uri) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || pm_metal_net_ip_bind(ls, addr, port) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("listen");
    }
    http_srv_t *srv = (http_srv_t *)pm_metal_async_coro_create(srv_step, sizeof(*srv));
    if (srv == NULL) {
        return fail("srv coro");
    }
    srv->ls = ls;
    srv->acc = -1;
    if (pm_metal_async_create_task(&srv->coro) == NULL) {
        return fail("srv task");
    }
    pm_metal_async_poll();
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st = pm_metal_wasm_io_fetch(uri, &body, &n, err, sizeof(err));
    (void)pm_metal_net_ip_close(ls);
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : (https ? "https fetch" : "fetch"));
    }
    if (n != 5 || body == NULL || memcmp(body, "hello", 5) != 0) {
        return fail("body");
    }
    return 0;
}

static int32_t case_fetch_lo(void) {
    return serve_and_fetch(LO4, HTTP_PORT, 0, step_server, "http://127.0.0.1:8080/x");
}

static int32_t case_https_lo(void) {
    if (!pm_metal_net_tls_ready()) {
        return fail("tls");
    }
    return serve_and_fetch(LO4, HTTPS_PORT, 1, step_https_server, "https://127.0.0.1:8444/x");
}

static int32_t case_fetch_sim(void) {
    int32_t h;
    if (pm_metal_drivers_net_sim_up() != 0) {
        return fail("sim up");
    }
    h = pm_metal_drivers_net_by_compat("sim", 0);
    if (h < 0 || pm_metal_net_ip_if_up_h(h, IF4) != 0) {
        return fail("sim if_up");
    }
    return serve_and_fetch(IF4, SIM_HTTP_PORT, 0, step_server, "http://10.0.0.1:8088/x");
}

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    int32_t ls;
    int32_t acc;
    uint8_t req[1024];
    uint32_t req_len;
} http_auth_srv_t;

static pm_metal_async_status_t step_auth_server(pm_metal_async_coro_t *self) {
    http_auth_srv_t *f = (http_auth_srv_t *)self;
    if (f->step == 0) {
        int32_t a = pm_metal_net_ip_accept(f->ls);
        if (a == -2) {
            return PM_METAL_ASYNC_WAITING;
        }
        if (a < 0) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->acc = a;
        f->step = 1;
    }
    uint8_t buf[256];
    int32_t n = pm_metal_net_ip_recv(f->acc, buf, sizeof(buf));
    if (n == 0) {
        return PM_METAL_ASYNC_WAITING;
    }
    if (n < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (f->req_len + (uint32_t)n >= sizeof(f->req)) {
        return PM_METAL_ASYNC_ERROR;
    }
    memcpy(f->req + f->req_len, buf, (uint32_t)n);
    f->req_len += (uint32_t)n;
    f->req[f->req_len] = 0;
    if (strstr((const char *)f->req, "\r\n\r\n") == NULL) {
        return PM_METAL_ASYNC_WAITING;
    }
    if (strstr((const char *)f->req, "Authorization: Bearer tok-cdn\r\n") == NULL
        || strstr((const char *)f->req, "X-Shell-Session-Id: sess-1\r\n") == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (pm_metal_net_ip_send(f->acc, k_resp, sizeof(k_resp) - 1u) < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    (void)pm_metal_net_ip_close(f->acc);
    return PM_METAL_ASYNC_DONE;
}

static int32_t case_auth_headers(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || pm_metal_net_ip_bind(ls, LO4, AUTH_PORT) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("auth listen");
    }
    http_auth_srv_t *srv = (http_auth_srv_t *)pm_metal_async_coro_create(step_auth_server, sizeof(*srv));
    if (srv == NULL) {
        return fail("auth srv");
    }
    srv->ls = ls;
    srv->acc = -1;
    if (pm_metal_async_create_task(&srv->coro) == NULL) {
        return fail("auth task");
    }
    pm_metal_async_poll();
    pm_wasmmod_io_set_auth_bearer("tok-cdn");
    pm_wasmmod_net_cdn_set_session_id("sess-1");
    uint8_t *body = NULL;
    uint32_t n = 0;
    char err[64];
    pm_wasmmod_io_result_t st =
        pm_metal_wasm_io_fetch("http://127.0.0.1:8082/x", &body, &n, err, sizeof(err));
    pm_wasmmod_io_set_auth_bearer(NULL);
    pm_wasmmod_net_cdn_set_session_id(NULL);
    (void)pm_metal_net_ip_close(ls);
    if (st != PM_WASMMOD_IO_OK) {
        return fail(err[0] ? err : "auth fetch");
    }
    if (n != 5 || body == NULL || memcmp(body, "hello", 5) != 0) {
        return fail("auth body");
    }
    return 0;
}

int32_t pm_metal_net_http_tests(void) {
    if (case_fetch_lo() != 0) {
        return 1;
    }
    if (case_https_lo() != 0) {
        return 1;
    }
    if (case_auth_headers() != 0) {
        return 1;
    }
    if (case_fetch_sim() != 0) {
        return 1;
    }
    return 0;
}
