#define _GNU_SOURCE /* ppoll in the host bridge, before any system header */

/* pymergetic.metal.net.fwd — host-socket forwarder for the unix seat.
 *
 * The Metal TCP stack is a self-contained in-process protocol stack over an L2
 * fill (sim on unix/emcc). Its listeners (.ssh, .http.asgi) therefore only ever
 * exist inside the process: `listen(0, port)` binds nothing on the host. This
 * card is the unix-seat analogue of the firmware QEMU `hostfwd` rules — it
 * mirrors each guest TCP listener onto a real AF_INET socket bound to
 * 0.0.0.0:<port> on the host, and shuttles bytes between an accepted host
 * connection and a *guest* client socket connected to 127.0.0.1:<port> over the
 * sim L2. Both sides speak the same bytestream protocol, so the ssh/asgi logic
 * is untouched: the bridge is purely transport.
 *
 * The guest listens are reachable from a guest 127.0.0.1 client even when bound
 * on ANY(0) — verified by the net.ip probe and relied on by the prove here:
 *   bind(0, port); listen; connect(127.0.0.1, port) => accept returns a child.
 *
 * The card builds on every seat (same face/signature), but the bridge body only
 * exists on a real Linux host build. Firmware and emcc compile the guards out
 * and their listen() reports no slot, exactly like how net/tap is a no-op off
 * Linux. No service registration: fwd is a transport mirror, not a user-facing
 * server — the run seat wires it for the concrete ports it serves.
 */
#include "pymergetic/metal/net/fwd/__exports__.h"

#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"

#include <string.h>
#include <time.h>

#define FWD_LO_BE 0x7f000001u
#define PM_METAL_FWD_MAX 8u
#define PM_METAL_FWD_CHUNK 1400u

#if !defined(PM_METAL_FIRMWARE) && !defined(__EMSCRIPTEN__)

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

struct fwd_conn {
    int host_fd;      /* accepted host connection; -1 = free */
    int32_t guest_fd; /* corresponding guest client socket; -1 = none */
    int connecting;   /* guest client connect handshake still in flight */
};

struct fwd_ep {
    uint16_t port;
    int listen_fd;         /* host listener on 0.0.0.0:port */
    pthread_t th;
    int running;           /* thread should keep looping */
    int active;            /* thread has started */
    struct fwd_conn conn[PM_METAL_FWD_MAX];
    uint8_t *guest_buf;    /* shuttled guest->host bytes */
};

static pm_util_mem_arena_t *s_arena;
static struct fwd_ep s_ep[PM_METAL_FWD_MAX];

static void fwd_set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) {
        (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
}

/* Close a guest socket, if any. path runs inside the fwd thread only. */
static void fwd_guest_close(struct fwd_ep *e, struct fwd_conn *c) {
    if (c->host_fd == e->listen_fd) {
        /* Someone handed us our own listener as a "client". Never close the
         * listener — that would strand the whole mirror. Just clear the slot. */
        c->guest_fd = -1;
        c->host_fd = -1;
        return;
    }
    if (c->guest_fd >= 0) {
        (void)pm_metal_net_ip_close(c->guest_fd);
    }
    c->guest_fd = -1;
    c->host_fd = -1;
}

/* Accept every pending host connection on an endpoint and open a guest client
 * to 127.0.0.1:port for each. Guest connect is asynchronous: the loopback
 * handshake returns 0 (pending) and parks this slot in *connecting* until
 * pm_metal_net_ip_established reports ESTAB; only a hard error (< 0) aborts. */
static void fwd_accept(struct fwd_ep *e) {
    for (;;) {
        int h = accept(e->listen_fd, NULL, NULL);
        if (h < 0) {
            return;
        }
        uint32_t i;
        struct fwd_conn *slot = NULL;
        for (i = 0; i < PM_METAL_FWD_MAX; i++) {
            if (e->conn[i].host_fd < 0) {
                slot = &e->conn[i];
                break;
            }
        }
        if (slot == NULL) {
            close(h);
            continue;
        }
        fwd_set_nonblock(h);
        int32_t g = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
        int crc = (g < 0) ? -99 : (int)pm_metal_net_ip_connect(g, FWD_LO_BE, e->port);
        if (g < 0 || crc < 0) {
            if (g >= 0) {
                (void)pm_metal_net_ip_close(g);
            }
            close(h);
            continue;
        }
        slot->host_fd = h;
        slot->guest_fd = g;
        slot->connecting = (crc == 0) ? 1 : 0; /* crc==1: already ESTAB */
    }
}

static void *fwd_pump(void *arg) {
    struct fwd_ep *e = arg;
    while (e->running) {
        struct pollfd pf[PM_METAL_FWD_MAX + 1];
        int nfds = 1;
        uint32_t i;
        /* Drive the guest stack each round so our guest client handshake, the
         * request delivery to the child, and the child's reply make progress
         * even when the SMP runners are idle on other work. Idempotent + cheap. */
        pm_metal_net_ip_pump();
        memset(pf, 0, sizeof(pf));
        pf[0].fd = e->listen_fd;
        pf[0].events = POLLIN;
        for (i = 0; i < PM_METAL_FWD_MAX; i++) {
            if (e->conn[i].host_fd >= 0) {
                pf[nfds].fd = e->conn[i].host_fd;
                pf[nfds].events = POLLIN;
                nfds++;
            }
        }
        /* poll() in this process is interrupted near-continuously by the async
         * SMP runner timer signals. Retry on EINTR within a 50ms budget so the
         * pump actually blocks instead of melting the CPU at 100k+ loop-ticks. */
        int r = -1;
        const struct timespec to = {0, 50 * 1000 * 1000};
        for (;;) {
            r = ppoll(pf, (nfds_t)nfds, &to, NULL);
            if (r >= 0 || errno != EINTR) {
                break;
            }
        }
        if (r < 0) {
            break;
        }
        if (r > 0 && pf[0].revents) {
            fwd_accept(e);
        }
        for (i = 0; i < PM_METAL_FWD_MAX; i++) {
            int idx = (int)i + 1;
            struct fwd_conn *c = &e->conn[i];
            if (c->host_fd < 0) {
                continue;
            }
            /* Advance an in-flight loopback connect to a live shuttled pair. */
            if (c->connecting) {
                int32_t st = pm_metal_net_ip_established(c->guest_fd);
                if (st != 1) {
                    if (st < 0) {
                        close(c->host_fd);
                        fwd_guest_close(e, c);
                    }
                    continue;
                }
                c->connecting = 0;
            }
            if (idx < nfds && (pf[idx].revents & (POLLIN | POLLHUP | POLLERR)) && !c->connecting) {
                /* Host has bytes or closed. */
                uint8_t buf[PM_METAL_FWD_CHUNK];
                ssize_t n = recv(c->host_fd, buf, sizeof(buf), 0);
                if (n > 0) {
                    (void)pm_metal_net_ip_send(c->guest_fd, buf, (uint32_t)n);
                } else if (n == 0) {
                    close(c->host_fd);
                    fwd_guest_close(e, c);
                    continue;
                } else {
                    int e_ = errno;
                    if (e_ == EBADF || e_ == ECONNRESET || e_ == ENOTCONN) {
                        close(c->host_fd);
                        fwd_guest_close(e, c);
                        continue;
                    }
                }
            }
            /* Guest -> host: pull whatever the guest socket has queued. */
            if (c->guest_fd >= 0 && !c->connecting) {
                int32_t n = pm_metal_net_ip_recv(c->guest_fd, e->guest_buf, PM_METAL_FWD_CHUNK);
                if (n > 0) {
                    ssize_t w = send(c->host_fd, e->guest_buf, (size_t)n, 0);
                    if (w <= 0) {
                        close(c->host_fd);
                        fwd_guest_close(e, c);
                        continue;
                    }
                } else if (n == -2) {
                    close(c->host_fd);
                    fwd_guest_close(e, c);
                }
            }
        }
    }
    return NULL;
}

int32_t pm_metal_fwd_init(pm_util_mem_arena_t *arena) {
    uint32_t i;
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_ep, 0, sizeof(s_ep));
    for (i = 0; i < PM_METAL_FWD_MAX; i++) {
        s_ep[i].listen_fd = -1;
        uint32_t k;
        for (k = 0; k < PM_METAL_FWD_MAX; k++) {
            s_ep[i].conn[k].host_fd = -1;
            s_ep[i].conn[k].guest_fd = -1;
        }
    }
    return 0;
}

/* poll() drives the pump; the prove's bounded pumping keeps tests deterministic. */
void pm_metal_fwd_deinit(void) {
    uint32_t i;
    for (i = 0; i < PM_METAL_FWD_MAX; i++) {
        struct fwd_ep *e = &s_ep[i];
        if (e->running) {
            e->running = 0;
            if (e->active) {
                (void)pthread_join(e->th, NULL);
            }
        }
        if (e->listen_fd >= 0) {
            close(e->listen_fd);
        }
        uint32_t j;
        for (j = 0; j < PM_METAL_FWD_MAX; j++) {
            if (e->conn[j].host_fd >= 0) {
                close(e->conn[j].host_fd);
            }
            if (e->conn[j].guest_fd >= 0) {
                (void)pm_metal_net_ip_close(e->conn[j].guest_fd);
            }
        }
    }
    memset(s_ep, 0, sizeof(s_ep));
    s_arena = NULL;
}

static int32_t fwd_open_listener(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 || listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }
    fwd_set_nonblock(fd);
    return fd;
}

int32_t pm_metal_fwd_listen(uint16_t port) {
    uint32_t i;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < PM_METAL_FWD_MAX; i++) {
        if (s_ep[i].running && s_ep[i].port == port) {
            return (int32_t)i;
        }
    }
    for (i = 0; i < PM_METAL_FWD_MAX; i++) {
        if (s_ep[i].running) {
            continue;
        }
        struct fwd_ep *e = &s_ep[i];
        uint32_t k;
        memset(e, 0, sizeof(*e));
        for (k = 0; k < PM_METAL_FWD_MAX; k++) {
            e->conn[k].host_fd = -1; /* free-slot sentinel; a fresh slot must be < 0 */
            e->conn[k].guest_fd = -1;
        }
        e->port = port;
        e->listen_fd = fwd_open_listener(port);
        if (e->listen_fd < 0) {
            return -1;
        }
        e->guest_buf = (uint8_t *)pm_util_mem_alloc(s_arena, PM_METAL_FWD_CHUNK);
        if (e->guest_buf == NULL) {
            close(e->listen_fd);
            e->listen_fd = -1;
            return -1;
        }
        e->running = 1;
        if (pthread_create(&e->th, NULL, fwd_pump, e) != 0) {
            e->running = 0;
            close(e->listen_fd);
            e->listen_fd = -1;
            return -1;
        }
        e->active = 1;
        return (int32_t)i;
    }
    return -1;
}

uint32_t pm_metal_fwd_count(void) {
    uint32_t i;
    uint32_t n = 0;
    for (i = 0; i < PM_METAL_FWD_MAX; i++) {
        if (s_ep[i].running) {
            n++;
        }
    }
    return n;
}

int32_t pm_metal_fwd_status(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_FWD_MAX) {
        return 0;
    }
    return s_ep[id].running ? 1 : 0;
}

int32_t pm_metal_fwd_stop(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_FWD_MAX) {
        return -1;
    }
    struct fwd_ep *e = &s_ep[id];
    if (!e->running) {
        return 0;
    }
    e->running = 0;
    if (e->active) {
        (void)pthread_join(e->th, NULL);
        e->active = 0;
    }
    if (e->listen_fd >= 0) {
        close(e->listen_fd);
        e->listen_fd = -1;
    }
    uint32_t j;
    for (j = 0; j < PM_METAL_FWD_MAX; j++) {
        if (e->conn[j].host_fd >= 0) {
            close(e->conn[j].host_fd);
        }
        if (e->conn[j].guest_fd >= 0) {
            (void)pm_metal_net_ip_close(e->conn[j].guest_fd);
        }
    }
    memset(e, 0, sizeof(*e));
    e->listen_fd = -1;
    uint32_t k;
    for (k = 0; k < PM_METAL_FWD_MAX; k++) {
        e->conn[k].host_fd = -1;
        e->conn[k].guest_fd = -1;
    }
    return 0;
}

#else /* firmware / emcc: build the same face, no host sockets. */

int32_t pm_metal_fwd_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    return 0;
}
void pm_metal_fwd_deinit(void) {}
int32_t pm_metal_fwd_listen(uint16_t port) {
    (void)port;
    return -1;
}
uint32_t pm_metal_fwd_count(void) {
    return 0;
}
int32_t pm_metal_fwd_status(int32_t id) {
    (void)id;
    return 0;
}
int32_t pm_metal_fwd_stop(int32_t id) {
    (void)id;
    return 0;
}

#endif

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.fwd, pm_metal_fwd_init, pm_metal_fwd_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.fwd, pm_metal_fwd_deinit, pm_metal_fwd_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.fwd, pm_metal_fwd_listen, pm_metal_fwd_listen, int32_t(uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.fwd, pm_metal_fwd_count, pm_metal_fwd_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.fwd, pm_metal_fwd_status, pm_metal_fwd_status, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.fwd, pm_metal_fwd_stop, pm_metal_fwd_stop, int32_t(int32_t));

PM_MOD_BOOT_C(pymergetic.metal.net.fwd, pm_metal_fwd_init, pm_metal_fwd_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.fwd, pymergetic.metal.net.ip);
