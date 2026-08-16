/* pymergetic.metal.net.tls — lo handshake + ping (host prove, not product). */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/tls.h"
#include "pymergetic/metal/net/tls/__testcert__.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define TLS_PORT 8443

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    int32_t ls;
    int32_t acc;
    pm_metal_net_tls_session_t *tls;
    uint8_t buf[16];
    int32_t n;
} tls_srv_t;

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    int32_t fd;
    pm_metal_net_tls_session_t *tls;
    uint8_t buf[16];
    int32_t n;
} tls_cli_t;

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.tls test: %s\n", why);
    return 1;
}

static pm_metal_async_status_t step_server(pm_metal_async_coro_t *self) {
    tls_srv_t *f = (tls_srv_t *)self;
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
    if (f->step == 2) {
        int32_t n = pm_metal_net_tls_recv(f->tls, f->buf, sizeof(f->buf));
        if (n == 0) {
            return PM_METAL_ASYNC_WAITING;
        }
        if (n < 0) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->n = n;
        f->step = 3;
    }
    if (pm_metal_net_tls_send(f->tls, f->buf, (uint32_t)f->n) < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    pm_metal_net_tls_close(f->tls);
    f->tls = NULL;
    (void)pm_metal_net_ip_close(f->acc);
    return PM_METAL_ASYNC_DONE;
}

static pm_metal_async_status_t step_client(pm_metal_async_coro_t *self) {
    tls_cli_t *f = (tls_cli_t *)self;
    if (f->step == 0) {
        f->fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
        if (f->fd < 0) {
            return PM_METAL_ASYNC_ERROR;
        }
        int32_t st = pm_metal_net_ip_connect(f->fd, LO4, TLS_PORT);
        if (st == 0) {
            f->step = 1;
            return PM_METAL_ASYNC_WAITING;
        }
        if (st < 0) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->step = 1;
    }
    if (f->step == 1) {
        f->tls = pm_metal_net_tls_client(f->fd, NULL);
        if (f->tls == NULL) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->step = 2;
    }
    if (f->step == 2) {
        int32_t st = pm_metal_net_tls_handshake(f->tls);
        if (st == 1) {
            return PM_METAL_ASYNC_WAITING;
        }
        if (st < 0) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->step = 3;
    }
    if (f->step == 3) {
        const uint8_t msg[] = { 'p', 'i', 'n', 'g' };
        if (pm_metal_net_tls_send(f->tls, msg, sizeof(msg)) != (int32_t)sizeof(msg)) {
            return PM_METAL_ASYNC_ERROR;
        }
        f->step = 4;
    }
    f->n = pm_metal_net_tls_recv(f->tls, f->buf, sizeof(f->buf));
    if (f->n == 0) {
        return PM_METAL_ASYNC_WAITING;
    }
    if (f->n < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    pm_metal_net_tls_close(f->tls);
    f->tls = NULL;
    (void)pm_metal_net_ip_close(f->fd);
    return PM_METAL_ASYNC_DONE;
}

static int32_t case_lo_ping(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || pm_metal_net_ip_bind(ls, LO4, TLS_PORT) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("listen");
    }
    tls_srv_t *srv = (tls_srv_t *)pm_metal_async_coro_create(step_server, sizeof(*srv));
    tls_cli_t *cli = (tls_cli_t *)pm_metal_async_coro_create(step_client, sizeof(*cli));
    if (srv == NULL || cli == NULL) {
        return fail("coro");
    }
    srv->ls = ls;
    srv->acc = -1;
    if (pm_metal_async_create_task(&srv->coro) == NULL || pm_metal_async_create_task(&cli->coro) == NULL) {
        return fail("task");
    }
    if (pm_metal_async_run_until(&cli->coro) != 0) {
        return fail("run");
    }
    (void)pm_metal_net_ip_close(ls);
    if (cli->n != 4 || memcmp(cli->buf, "ping", 4) != 0) {
        return fail("echo");
    }
    return 0;
}

int32_t pm_metal_net_tls_tests(void) {
    if (!pm_metal_net_tls_ready()) {
        return fail("tls not ready");
    }
    return case_lo_ping();
}
