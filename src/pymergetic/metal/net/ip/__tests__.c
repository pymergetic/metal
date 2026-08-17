/* pymergetic.metal.net.ip — lo ping + UDP park prove. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    int32_t fd;
    uint8_t buf[16];
    int32_t n;
} udp_rx_frame_t;

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.ip test: %s\n", why);
    return 1;
}

static int32_t case_ping_lo(void) {
    const uint8_t payload[] = { 'p', 'i', 'n', 'g' };
    uint8_t out[64];
    uint32_t n = sizeof(out);
    if (pm_metal_net_ip_ping4(LO4, payload, sizeof(payload), out, &n) != 0) {
        return fail("ping");
    }
    if (n < 8u + sizeof(payload) || out[0] != 0) {
        return fail("echo reply");
    }
    if (memcmp(out + 8, payload, sizeof(payload)) != 0) {
        return fail("payload");
    }
    return 0;
}

static int32_t case_udp_lo_sync(void) {
    int32_t a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (a < 0 || b < 0) {
        return fail("socket");
    }
    if (pm_metal_net_ip_bind(a, LO4, 7001) != 0 || pm_metal_net_ip_bind(b, LO4, 7002) != 0) {
        return fail("bind");
    }
    const uint8_t msg[] = { 'h', 'i' };
    if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), LO4, 7002) != (int32_t)sizeof(msg)) {
        return fail("sendto");
    }
    uint8_t buf[8];
    uint32_t addr = 0;
    uint16_t port = 0;
    int32_t n = pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), &addr, &port);
    (void)pm_metal_net_ip_close(a);
    (void)pm_metal_net_ip_close(b);
    if (n != 2 || buf[0] != 'h' || port != 7001) {
        return fail("recvfrom");
    }
    return 0;
}

static pm_metal_async_status_t step_udp_rx(pm_metal_async_coro_t *self) {
    udp_rx_frame_t *f = (udp_rx_frame_t *)self;
    if (f->step == 0) {
        f->n = pm_metal_net_ip_recvfrom(f->fd, f->buf, sizeof(f->buf), NULL, NULL);
        if (f->n == 0) {
            f->step = 1;
            return PM_METAL_ASYNC_WAITING;
        }
        return f->n > 0 ? PM_METAL_ASYNC_DONE : PM_METAL_ASYNC_ERROR;
    }
    f->n = pm_metal_net_ip_recvfrom(f->fd, f->buf, sizeof(f->buf), NULL, NULL);
    return f->n > 0 ? PM_METAL_ASYNC_DONE : PM_METAL_ASYNC_ERROR;
}

static int32_t case_udp_park(void) {
    int32_t rx = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t tx = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (rx < 0 || tx < 0) {
        return fail("park socket");
    }
    if (pm_metal_net_ip_bind(rx, LO4, 7101) != 0 || pm_metal_net_ip_bind(tx, LO4, 7102) != 0) {
        return fail("park bind");
    }
    udp_rx_frame_t *f = (udp_rx_frame_t *)pm_metal_async_coro_create(step_udp_rx, sizeof(*f));
    if (f == NULL) {
        return fail("park coro");
    }
    f->fd = rx;
    pm_metal_async_task_t *t = pm_metal_async_create_task(&f->coro);
    if (t == NULL) {
        return fail("park task");
    }
    /* First poll parks on empty recv. */
    pm_metal_async_poll();
    if (f->coro.status != PM_METAL_ASYNC_WAITING) {
        return fail("not parked");
    }
    const uint8_t msg[] = { 'z' };
    if (pm_metal_net_ip_sendto(tx, msg, 1, LO4, 7101) != 1) {
        return fail("park send");
    }
    if (pm_metal_async_run(t) != 0) {
        return fail("park run");
    }
    (void)pm_metal_net_ip_close(rx);
    (void)pm_metal_net_ip_close(tx);
    if (f->n != 1 || f->buf[0] != 'z') {
        return fail("park data");
    }
    return 0;
}

static int32_t case_tcp_lo_echo(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    int32_t cl = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || cl < 0) {
        return fail("tcp socket");
    }
    if (pm_metal_net_ip_bind(ls, LO4, 9000) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("tcp listen");
    }
    if (pm_metal_net_ip_connect(cl, LO4, 9000) != 1) {
        return fail("tcp connect");
    }
    int32_t sv = pm_metal_net_ip_accept(ls);
    if (sv < 0) {
        return fail("tcp accept");
    }
    const uint8_t msg[] = { 'a', 'b' };
    if (pm_metal_net_ip_send(cl, msg, sizeof(msg)) != 2) {
        return fail("tcp send");
    }
    uint8_t buf[8];
    int32_t n = pm_metal_net_ip_recv(sv, buf, sizeof(buf));
    if (n != 2 || buf[0] != 'a') {
        return fail("tcp recv");
    }
    if (pm_metal_net_ip_send(sv, buf, 2) != 2) {
        return fail("tcp reply");
    }
    n = pm_metal_net_ip_recv(cl, buf, sizeof(buf));
    (void)pm_metal_net_ip_close(cl);
    (void)pm_metal_net_ip_close(sv);
    (void)pm_metal_net_ip_close(ls);
    if (n != 2 || buf[1] != 'b') {
        return fail("tcp echo");
    }
    return 0;
}

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    int32_t ls;
    int32_t acc;
} tcp_acc_frame_t;

static pm_metal_async_status_t step_tcp_accept(pm_metal_async_coro_t *self) {
    tcp_acc_frame_t *f = (tcp_acc_frame_t *)self;
    int32_t a = pm_metal_net_ip_accept(f->ls);
    if (a == -2) {
        f->step = 1;
        return PM_METAL_ASYNC_WAITING;
    }
    if (a < 0) {
        return PM_METAL_ASYNC_ERROR;
    }
    f->acc = a;
    return PM_METAL_ASYNC_DONE;
}

static int32_t case_tcp_accept_park(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    int32_t cl = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || cl < 0) {
        return fail("acc socket");
    }
    if (pm_metal_net_ip_bind(ls, LO4, 9001) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("acc listen");
    }
    tcp_acc_frame_t *f = (tcp_acc_frame_t *)pm_metal_async_coro_create(step_tcp_accept, sizeof(*f));
    if (f == NULL) {
        return fail("acc coro");
    }
    f->ls = ls;
    f->acc = -1;
    pm_metal_async_task_t *t = pm_metal_async_create_task(&f->coro);
    if (t == NULL) {
        return fail("acc task");
    }
    pm_metal_async_poll();
    if (f->coro.status != PM_METAL_ASYNC_WAITING) {
        return fail("acc not parked");
    }
    if (pm_metal_net_ip_connect(cl, LO4, 9001) != 1) {
        return fail("acc connect");
    }
    if (pm_metal_async_run(t) != 0 || f->acc < 0) {
        return fail("acc run");
    }
    (void)pm_metal_net_ip_close(f->acc);
    (void)pm_metal_net_ip_close(cl);
    (void)pm_metal_net_ip_close(ls);
    return 0;
}

int32_t pm_metal_net_ip_tests(void) {
    if (case_ping_lo() != 0) {
        return 1;
    }
    if (case_udp_lo_sync() != 0) {
        return 1;
    }
    if (case_udp_park() != 0) {
        return 1;
    }
    if (case_tcp_lo_echo() != 0) {
        return 1;
    }
    if (case_tcp_accept_park() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.ip, tests, pm_metal_net_ip_tests);
