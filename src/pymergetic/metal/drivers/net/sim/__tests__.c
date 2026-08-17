/* pymergetic.metal.drivers.net.sim — Ethernet loop UDP + TCP rexmit after drop. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/drivers/net/sim.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define IF4 0x0a000001u

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.net.sim test: %s\n", why);
    return 1;
}

static void pump_n(uint32_t n) {
    while (n != 0) {
        pm_metal_net_ip_pump();
        n--;
    }
}

static void wait_us(uint64_t us) {
    struct timespec ts;
    ts.tv_sec = (time_t)(us / 1000000ull);
    ts.tv_nsec = (long)((us % 1000000ull) * 1000ull);
    (void)clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

static int32_t case_udp(void) {
    int32_t h;
    if (pm_metal_drivers_net_sim_up() != 0) {
        return fail("up");
    }
    h = pm_metal_drivers_net_by_compat("sim", 0);
    if (h < 0 || pm_metal_net_ip_if_up_h(h, IF4) != 0) {
        return fail("if_up_h");
    }
    int32_t a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (a < 0 || b < 0) {
        return fail("socket");
    }
    if (pm_metal_net_ip_bind(a, IF4, 9101) != 0 || pm_metal_net_ip_bind(b, IF4, 9102) != 0) {
        return fail("bind");
    }
    const uint8_t msg[] = { 's', 'i' };
    if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), IF4, 9102) != (int32_t)sizeof(msg)) {
        return fail("sendto");
    }
    pm_metal_net_ip_pump();
    uint8_t buf[8];
    uint16_t port = 0;
    int32_t n = pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port);
    (void)pm_metal_net_ip_close(a);
    (void)pm_metal_net_ip_close(b);
    if (n != 2 || buf[0] != 's' || port != 9101) {
        return fail("recvfrom");
    }
    return 0;
}

static int32_t tcp_ready(int32_t ls, int32_t cl, uint16_t port, int32_t *sv) {
    int32_t st;
    uint32_t i;
    if (pm_metal_net_ip_bind(cl, IF4, (uint16_t)(port + 1000u)) != 0) {
        return fail("client bind");
    }
    st = pm_metal_net_ip_connect(cl, IF4, port);
    if (st < 0) {
        return fail("connect");
    }
    for (i = 0; i < 8u; i++) {
        pm_metal_net_ip_pump();
        *sv = pm_metal_net_ip_accept(ls);
        if (*sv >= 0) {
            return 0;
        }
    }
    return fail("accept");
}

static int32_t case_tcp_echo(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    int32_t cl = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    int32_t sv = -1;
    uint8_t buf[8];
    int32_t n;
    const uint8_t msg[] = { 'a', 'b' };
    if (ls < 0 || cl < 0) {
        return fail("tcp socket");
    }
    if (pm_metal_net_ip_bind(ls, IF4, 9200) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("tcp listen");
    }
    if (tcp_ready(ls, cl, 9200, &sv) != 0) {
        return 1;
    }
    if (pm_metal_net_ip_send(cl, msg, sizeof(msg)) != 2) {
        return fail("tcp send");
    }
    pump_n(8);
    n = pm_metal_net_ip_recv(sv, buf, sizeof(buf));
    (void)pm_metal_net_ip_close(cl);
    (void)pm_metal_net_ip_close(sv);
    (void)pm_metal_net_ip_close(ls);
    if (n != 2 || buf[0] != 'a') {
        return fail("tcp recv");
    }
    return 0;
}

static int32_t case_tcp_rexmit(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    int32_t cl = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    int32_t sv = -1;
    uint8_t buf[8];
    int32_t n;
    const uint8_t msg[] = { 'r', 'x' };
    if (ls < 0 || cl < 0) {
        return fail("rexmit socket");
    }
    if (pm_metal_net_ip_bind(ls, IF4, 9201) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("rexmit listen");
    }
    if (tcp_ready(ls, cl, 9201, &sv) != 0) {
        return 1;
    }
    if (pm_metal_drivers_net_sim_drop(1) != 0) {
        return fail("drop");
    }
    if (pm_metal_net_ip_send(cl, msg, sizeof(msg)) != 2) {
        return fail("rexmit send");
    }
    pump_n(2);
    n = pm_metal_net_ip_recv(sv, buf, sizeof(buf));
    if (n > 0) {
        return fail("dropped data arrived");
    }
    wait_us(80000ull);
    pump_n(2);
    n = pm_metal_net_ip_recv(sv, buf, sizeof(buf));
    (void)pm_metal_net_ip_close(cl);
    (void)pm_metal_net_ip_close(sv);
    (void)pm_metal_net_ip_close(ls);
    if (n != 2 || buf[0] != 'r') {
        return fail("rexmit recv");
    }
    return 0;
}

int32_t pm_metal_drivers_net_sim_tests(void) {
    if (case_udp() != 0) {
        return 1;
    }
    if (case_tcp_echo() != 0) {
        return 1;
    }
    if (case_tcp_rexmit() != 0) {
        return 1;
    }
    {
        int32_t ha;
        int32_t hb;
        int32_t dt;
        int32_t a;
        int32_t b;
        int32_t rx;
        uint8_t buf[8];
        uint16_t port = 0;
        const uint8_t msg[] = { '2', 'n' };
        const uint32_t a4 = 0x0a000001u;
        const uint32_t b4 = 0x0a010001u;
        ha = pm_metal_drivers_net_sim_probe();
        hb = pm_metal_drivers_net_sim_probe();
        if (ha < 0 || hb < 0 || ha == hb) {
            return fail("probe two");
        }
        if (pm_metal_net_ip_if_up_h(ha, a4) != 0 || pm_metal_net_ip_if_up_h(hb, b4) != 0) {
            return fail("if_up_h");
        }
        if (pm_metal_net_ip_if_addr(ha) != a4 || pm_metal_net_ip_if_addr(hb) != b4) {
            return fail("if_addr");
        }
        a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        if (a < 0 || b < 0 || pm_metal_net_ip_bind(a, a4, 9501) != 0
            || pm_metal_net_ip_bind(b, a4, 9502) != 0) {
            return fail("nic a bind");
        }
        if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), a4, 9502) != 2) {
            return fail("nic a sendto");
        }
        pm_metal_net_ip_pump();
        if (pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port) != 2 || buf[0] != '2') {
            return fail("nic a recv");
        }
        rx = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        if (rx < 0 || pm_metal_net_ip_bind_l2(rx, hb) != 0 || pm_metal_net_ip_bind(rx, b4, 9503) != 0) {
            return fail("bind_l2");
        }
        if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), b4, 9503) != 2) {
            return fail("cross sendto");
        }
        pm_metal_net_ip_pump();
        if (pm_metal_net_ip_recvfrom(rx, buf, sizeof(buf), NULL, &port) > 0) {
            return fail("bind_l2 leaked ha rx");
        }
        if (pm_metal_net_ip_bind(a, b4, 9504) != 0 || pm_metal_net_ip_bind(b, b4, 9505) != 0) {
            return fail("nic b bind");
        }
        if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), b4, 9505) != 2) {
            return fail("nic b sendto");
        }
        pm_metal_net_ip_pump();
        if (pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port) != 2 || buf[0] != '2') {
            return fail("nic b recv");
        }
        dt = pm_metal_drivers_net_dt_id(hb);
        if (dt < 0 || pm_metal_drivers_unbind(dt) != 0) {
            return fail("unbind hb");
        }
        if (pm_metal_net_ip_bind(a, a4, 9506) != 0 || pm_metal_net_ip_bind(b, a4, 9507) != 0) {
            return fail("after unbind bind");
        }
        if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), a4, 9507) != 2) {
            return fail("after unbind sendto");
        }
        pm_metal_net_ip_pump();
        if (pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port) != 2 || buf[0] != '2') {
            return fail("after unbind recv");
        }
        (void)pm_metal_net_ip_close(a);
        (void)pm_metal_net_ip_close(b);
        (void)pm_metal_net_ip_close(rx);
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.net.sim, tests, pm_metal_drivers_net_sim_tests);
