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
#include <stdlib.h>
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

/* A real host client on loopback, the way anything outside the seat arrives. */
static int host_connect(uint16_t port) {
    struct sockaddr_in sa;
    int h = socket(AF_INET, SOCK_STREAM, 0);
    if (h < 0) {
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(0x7f000001u);
    sa.sin_port = htons(port);
    if (connect(h, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(h);
        return -1;
    }
    return h;
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

/* Large guest->host transfer through fwd: the scenario behind a browser loading
 * a multi-KiB page/download. A guest server sends a 200 KiB body in several
 * socket writes (far beyond one rx window); fwd must drain-and-shuttle each
 * opened window so the whole body reaches the real host client intact. Guards
 * the window-park/thrash fix (a small fwd drain chunk used to stall this). */
#define FWD_LARGE_N 204800u /* 200 KiB */

static int32_t case_fwd_large(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || pm_metal_net_ip_bind(ls, 0u, FWD_PORT) != 0
        || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("large guest listen");
    }
    int32_t fid = pm_metal_fwd_listen(FWD_PORT);
    if (fid < 0) {
        (void)pm_metal_net_ip_close(ls);
        return fail("large fwd listen");
    }
    int h = socket(AF_INET, SOCK_STREAM, 0);
    if (h < 0) {
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(ls);
        return fail("large host socket");
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
        return fail("large host connect");
    }
    int32_t sv;
    if (pump_until_accept(ls, &sv) != 0) {
        close(h);
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(ls);
        return fail("large guest accept");
    }
    /* Synthetic body: byte i == i mod 251, written in 16 KiB pushes from the
     * guest server so the stack must reopen its window repeatedly. */
    static uint8_t chunk[16384];
    uint32_t written = 0;
    while (written < FWD_LARGE_N) {
        uint32_t take = FWD_LARGE_N - written;
        if (take > sizeof(chunk)) {
            take = sizeof(chunk);
        }
        for (uint32_t i = 0; i < take; i++) {
            chunk[i] = (uint8_t)((written + i) % 251u);
        }
        int32_t s = pm_metal_net_ip_send(sv, chunk, take);
        if (s < 0) {
            close(h);
            (void)pm_metal_net_ip_close(sv);
            (void)pm_metal_fwd_stop(fid);
            (void)pm_metal_net_ip_close(ls);
            return fail("large guest send");
        }
        written += (uint32_t)s;
        pm_metal_net_ip_pump();
        nap();
    }
    /* Verify on the host socket, pumping the guest so fwd keeps shuttling. */
    uint8_t *body = (uint8_t *)malloc(FWD_LARGE_N);
    if (body == NULL || host_recv_all(h, body, FWD_LARGE_N) != 0) {
        if (body != NULL) {
            free(body);
        }
        close(h);
        (void)pm_metal_net_ip_close(sv);
        (void)pm_metal_fwd_stop(fid);
        (void)pm_metal_net_ip_close(ls);
        return fail("large host recv");
    }
    for (uint32_t i = 0; i < FWD_LARGE_N; i++) {
        if (body[i] != (uint8_t)(i % 251u)) {
            free(body);
            close(h);
            (void)pm_metal_net_ip_close(sv);
            (void)pm_metal_fwd_stop(fid);
            (void)pm_metal_net_ip_close(ls);
            return fail("large body mismatch");
        }
    }
    free(body);
    close(h);
    (void)pm_metal_net_ip_close(sv);
    (void)pm_metal_net_ip_close(ls);
    (void)pm_metal_fwd_stop(fid);
    return 0;
}

/* Slots must come back. The bridge has PM_METAL_FWD_MAX of them, so a seat that
 * leaks one per connection dies after a handful of page loads — and it dies for
 * every route, not just the one that leaked. Cycling more clients than there are
 * slots, closing each, proves the reap path: the poll array is packed, so this
 * also pins the conn->pollfd mapping that a naive index walk gets wrong once any
 * earlier slot is free. */
static int32_t case_fwd_slot_reuse(void) {
    /* Comfortably more than the bridge's private slot count, so a table that
     * never reaps runs dry partway through. */
    const uint32_t cycles = 24u;
    uint32_t k;
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || pm_metal_net_ip_bind(ls, 0u, (uint16_t)(FWD_PORT + 2u)) != 0
        || pm_metal_net_ip_listen(ls, 8) != 0) {
        return fail("reuse: guest listen");
    }
    if (pm_metal_fwd_listen((uint16_t)(FWD_PORT + 2u)) < 0) {
        return fail("reuse: fwd listen");
    }
    for (k = 0; k < cycles; k++) {
        int32_t child = -1;
        int hs = host_connect((uint16_t)(FWD_PORT + 2u));
        if (hs < 0) {
            return fail("reuse: host connect");
        }
        if (pump_until_accept(ls, &child) != 0) {
            close(hs);
            return fail("reuse: bridge stopped accepting");
        }
        (void)pm_metal_net_ip_close(child);
        close(hs);
        /* Let the pump notice both ends and hand the slot back. */
        {
            int i;
            for (i = 0; i < 200; i++) {
                pm_metal_net_ip_pump();
                nap();
            }
        }
    }
    (void)pm_metal_net_ip_close(ls);
    return 0;
}

static int32_t pm_metal_net_fwd_tests(void) {
    if (case_fwd_roundtrip() != 0) {
        return 1;
    }
    if (case_fwd_large() != 0) {
        return 1;
    }
    return case_fwd_slot_reuse();
}

PM_MOD_TEST_C(pymergetic.metal.net.fwd, fwd, pm_metal_net_fwd_tests);
#endif /* host only */
