/* pymergetic.metal.net.fwd — host-socket mirror of guest TCP listeners.
 *
 * Prove: a real AF_INET client on the host can reach a *guest* Metal TCP server
 * bound on ANY, through the fwd bridge, and get symmetric bytes back — the same
 * transport the firmware seats get from QEMU's 0.0.0.0 hostfwd.
 *
 *  guest server (ANY) <--sim L2-- guest client (opened by fwd) <--shuttle-->
 *                                                            host socket
 */
#include "pymergetic/metal/net/fwd.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(PM_METAL_FIRMWARE) && !defined(__EMSCRIPTEN__)
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FWD_PORT 19001u

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.fwd test: %s\n", why);
    return 1;
}

static void nap(void) {
    struct timespec ts = {0, 2000 * 1000u};
    nanosleep(&ts, NULL);
}

/* Pump guest stack until the server's accept queue yields a child. */
static int pump_until_accept(int32_t ls, int32_t *child) {
    int i;
    for (i = 0; i < 500; i++) {
        *child = pm_metal_net_ip_accept(ls);
        if (*child >= 0) {
            return 0;
        }
        pm_metal_net_ip_pump();
        nap();
    }
    return -1;
}

/* Pull all currently-queued bytes on a guest fd up to `want`; pump to deliver
 * whatever is still in the sim queue. */
static int guest_recv_all(int32_t fd, uint8_t *buf, uint32_t *len, uint32_t want, int final) {
    int i;
    uint32_t got = *len;
    for (i = 0; i < 500; i++) {
        if (got < want) {
            int32_t k = pm_metal_net_ip_recv(fd, buf + got, want - got);
            if (k > 0) {
                got += (uint32_t)k;
                if (got >= want) {
                    *len = got;
                    return 0;
                }
                continue;
            }
        }
        if (!final && got != 0) {
            *len = got;
            return 0;
        }
        pm_metal_net_ip_pump();
        nap();
    }
    *len = got;
    return got >= want ? 0 : -1;
}

/* Read `want` bytes from a real host socket, pumping the guest stack so the fwd
 * thread can drain guest->host traffic meanwhile. */
static int host_recv_all(int fd, uint8_t *buf, size_t want) {
    size_t got = 0;
    int i;
    for (i = 0; i < 600; i++) {
        while (got < want) {
            ssize_t n = recv(fd, buf + got, want - got, 0);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (n <= 0) {
                return -1;
            }
            got += (size_t)n;
        }
        if (got >= want) {
            return 0;
        }
        pm_metal_net_ip_pump();
        nap();
    }
    return -1;
}

static int32_t case_fwd_roundtrip(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || pm_metal_net_ip_bind(ls, 0u, FWD_PORT) != 0
        || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("guest listen");
    }
    int32_t fid = pm_metal_fwd_listen(FWD_PORT);
    if (fid < 0) {
        (void)pm_metal_net_ip_close(ls);
        return fail("fwd listen");
    }
    /* dup listen returns the same running id. */
    if (pm_metal_fwd_listen(FWD_PORT) != fid) {
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(ls);
        return fail("dup listen not idempotent");
    }
    if (pm_metal_fwd_count() < 1 || pm_metal_fwd_status(fid) != 1) {
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(ls);
        return fail("count/status");
    }
    /* Real host client. */
    int h = socket(AF_INET, SOCK_STREAM, 0);
    if (h < 0) {
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(ls);
        return fail("host socket");
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(0x7f000001u);
    sa.sin_port = htons(FWD_PORT);
    if (connect(h, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(h);
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(ls);
        return fail("host connect");
    }
    /* fwd accepted the host conn and opened a guest client; pump until the
     * guest server accepts that client. */
    int32_t sv;
    if (pump_until_accept(ls, &sv) != 0) {
        close(h);
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(ls);
        return fail("guest accept of fwd client");
    }
    /* host -> fwd -> guest */
    static const char ping[] = "ping";
    if (send(h, ping, sizeof(ping) - 1u, 0) != (ssize_t)(sizeof(ping) - 1u)) {
        close(h);
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(sv);
        (void)pm_metal_net_ip_close(ls);
        return fail("host send");
    }
    uint8_t g[8];
    uint32_t gn = 0;
    if (guest_recv_all(sv, g, &gn, (uint32_t)(sizeof(ping) - 1u), 1) != 0
        || gn != sizeof(ping) - 1u || memcmp(g, ping, sizeof(ping) - 1u) != 0) {
        close(h);
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(sv);
        (void)pm_metal_net_ip_close(ls);
        return fail("guest did not receive host bytes");
    }
    /* guest -> fwd -> host */
    static const char pong[] = "pong";
    if (pm_metal_net_ip_send(sv, (const uint8_t *)pong, (uint32_t)(sizeof(pong) - 1u))
        != (int32_t)(sizeof(pong) - 1u)) {
        close(h);
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(sv);
        (void)pm_metal_net_ip_close(ls);
        return fail("guest send");
    }
    uint8_t back[8];
    if (host_recv_all(h, back, sizeof(pong) - 1u) != 0
        || memcmp(back, pong, sizeof(pong) - 1u) != 0) {
        close(h);
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(sv);
        (void)pm_metal_net_ip_close(ls);
        return fail("host did not receive guest bytes");
    }
    close(h);
    (void)pm_metal_net_ip_close(sv);
    (void)pm_metal_net_ip_close(ls);
    /* stop tears the endpoint down and status drops to 0. */
    if (pm_metal_fwd_stop(fid) != 0 || pm_metal_fwd_status(fid) != 0) {
        return fail("stop/status");
    }
    return 0;
}

static int32_t pm_metal_net_fwd_tests(void) {
    return case_fwd_roundtrip();
}

PM_MOD_TEST_C(pymergetic.metal.net.fwd, fwd, pm_metal_net_fwd_tests);
#endif /* host only */
