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

#include "pymergetic/util/limits.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"

#include <string.h>
#include <time.h>

#define FWD_LO_BE 0x7f000001u
/* Mirrors this seat starts with, and connections each one carries. Both are
 * where it starts: a mirror is taken from the arena when a port is mirrored,
 * its connections when it opens, and a seat expecting more traffic says so
 * (`m.limit("net.fwd.connection", 64)`) rather than being rebuilt. */
#define PM_METAL_FWD_MIRROR_DEFAULT 8u
#define PM_METAL_FWD_CONN_DEFAULT 8u
/* Drain granularity for guest->host shuttling. Must be >= the guest TCP rx
 * window (net.ip.receive) so one pump tick can empty the whole guest
 * client buffer. A small chunk (e.g. 1400) would free only that much of the
 * send window per tick, thrash the sender's window park, and stall large
 * responses — a browser hanging on a big page/download. */
#define PM_METAL_FWD_CHUNK_DEFAULT 8192u

/* Declared on every seat, whatever the seat can do with them, so a listing
 * reads the same everywhere. */
static uint32_t s_mirror_used;
PM_UTIL_LIMIT_C(pm_fwd_limit_mirror, "net.fwd.mirror", PM_METAL_FWD_MIRROR_DEFAULT, 0u, &s_mirror_used);
PM_UTIL_LIMIT_C(pm_fwd_limit_conn, "net.fwd.connection", PM_METAL_FWD_CONN_DEFAULT, 0u, NULL);
PM_UTIL_LIMIT_C(pm_fwd_limit_chunk, "net.fwd.chunk", PM_METAL_FWD_CHUNK_DEFAULT, 1048576u, NULL);

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
    /* Bytes already taken out of the guest that the host socket has not
     * accepted yet. The host end is non-blocking, so a reader slower than the
     * seat — a browser pulling a 26 MB image — fills the kernel's send buffer
     * and send() refuses the rest. Those bytes are gone from the guest's ring
     * by then, so they wait here until the socket drains. Nothing is read from
     * the guest while this is occupied, which is what makes the slow reader
     * slow the seat down instead of losing its download. */
    uint8_t *pend;
    uint32_t pend_len;
    uint32_t pend_off;
};

/* A mirror. Everything its pump thread touches is allocated with it and never
 * moves afterwards: the thread runs with no arena of its own, and a knob moved
 * while it runs applies to the next mirror, not to this one mid-transfer. */
struct fwd_ep {
    uint16_t port;
    int listen_fd;         /* host listener on 0.0.0.0:port */
    pthread_t th;
    int running;           /* thread should keep looping */
    int active;            /* thread has started */
    struct fwd_conn *conn;
    uint32_t conn_cap;
    uint32_t chunk;        /* the shuttle size this mirror started with */
    uint8_t *hostbuf;      /* host -> guest staging, one per mirror */
    struct pollfd *pf;     /* listener plus one per connection */
    int *pfi;              /* which pf slot each connection went into */
};

static pm_util_mem_arena_t *s_arena;
/* Mirrors, allocated one at a time: a pump thread holds its own endpoint for
 * as long as it runs, so the endpoints themselves must never be moved. Only
 * the table of pointers to them grows. */
static struct fwd_ep **s_ep;
static uint32_t s_ep_cap;

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
    /* Whatever this connection still held belongs to a transfer that is over.
     * The slot is handed to the next client, who must not inherit its tail. */
    c->pend_len = 0;
    c->pend_off = 0;
}

/* Accept every pending host connection on an endpoint and open a guest client
 * to 127.0.0.1:port for each. Guest connect is asynchronous: the loopback
 * handshake returns 0 (pending) and parks this slot in *connecting* until
 * pm_metal_net_ip_established reports ESTAB; only a hard error (< 0) aborts. */
static void fwd_accept(struct fwd_ep *e) {
    for (;;) {
        uint32_t i;
        struct fwd_conn *slot = NULL;
        int h;
        for (i = 0; i < e->conn_cap; i++) {
            if (e->conn[i].host_fd < 0) {
                slot = &e->conn[i];
                break;
            }
        }
        /* Every slot busy: leave the client in the listener's backlog rather
         * than accepting it only to hang up. A browser fetching an image while
         * the console panel polls runs several connections at once, and a
         * reset in the middle of that reads as "the seat broke", not "the seat
         * is busy". It waits instead, and is served when a slot frees. */
        if (slot == NULL) {
            return;
        }
        h = accept(e->listen_fd, NULL, NULL);
        if (h < 0) {
            return;
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

/* Push whatever is held for this connection at the host socket. Returns the
 * bytes moved, or -1 when the connection is done for. A socket that will not
 * take them right now is not an error — that is a reader keeping up badly, and
 * the tail waits for the POLLOUT that says it has room. */
static ssize_t fwd_flush(struct fwd_conn *c) {
    ssize_t moved = 0;
    while (c->pend_off < c->pend_len) {
        ssize_t w = send(c->host_fd, c->pend + c->pend_off,
            (size_t)(c->pend_len - c->pend_off), 0);
        if (w > 0) {
            c->pend_off += (uint32_t)w;
            moved += w;
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return moved;
        }
        return -1;
    }
    c->pend_off = 0;
    c->pend_len = 0;
    return moved;
}

static void *fwd_pump(void *arg) {
    struct fwd_ep *e = arg;
    /* How long to wait before looking again. The poll watches host sockets
     * only — the guest side of the mirror is not a pollable thing, it is a ring
     * this thread has to come back and read — so a fixed wait is paid once per
     * guest window of a download. At 8 KiB a window that is the difference
     * between line rate and a trickle.
     *
     * So: back to work immediately after a round that moved bytes, and after an
     * empty one double the wait from 200 µs up to the idle 50 ms. A transfer
     * sits at the floor, an idle mirror costs twenty wakeups a second. Same
     * ladder the coop runners use, for the same reason. */
    long wait_ns = 0;
    while (e->running) {
        /* The poll set and the map beside it belong to this mirror (pf, pfi).
         * The map matters because the set is packed — free connections are
         * skipped, so conn[i] is not pf[i + 1] once any earlier slot is free,
         * and reading the wrong revents loses the peer's POLLHUP and leaks
         * the connection for good. */
        struct pollfd *pf = e->pf;
        int *pfi = e->pfi;
        int nfds = 1;
        uint32_t i;
        /* Drive the guest stack each round so our guest client handshake, the
         * request delivery to the child, and the child's reply make progress
         * even when the SMP runners are idle on other work. Idempotent + cheap. */
        pm_metal_net_ip_pump();
        memset(pf, 0, ((size_t)e->conn_cap + 1u) * sizeof(*pf));
        pf[0].fd = e->listen_fd;
        pf[0].events = POLLIN;
        for (i = 0; i < e->conn_cap; i++) {
            pfi[i] = -1;
            if (e->conn[i].host_fd >= 0) {
                pf[nfds].fd = e->conn[i].host_fd;
                pf[nfds].events = POLLIN;
                if (e->conn[i].pend_off < e->conn[i].pend_len) {
                    pf[nfds].events |= POLLOUT;
                }
                pfi[i] = nfds;
                nfds++;
            }
        }
        /* poll() in this process is interrupted near-continuously by the async
         * SMP runner timer signals. Retry on EINTR within a 50ms budget so the
         * pump actually blocks instead of melting the CPU at 100k+ loop-ticks. */
        int r = -1;
        int worked = 0;
        const struct timespec to = {0, wait_ns};
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
        for (i = 0; i < e->conn_cap; i++) {
            int idx = pfi[i];
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
            if (idx > 0 && (pf[idx].revents & (POLLIN | POLLHUP | POLLERR)) && !c->connecting) {
                /* Host has bytes or closed. */
                ssize_t n = recv(c->host_fd, e->hostbuf, (size_t)e->chunk, 0);
                if (n > 0) {
                    (void)pm_metal_net_ip_send(c->guest_fd, e->hostbuf, (uint32_t)n);
                    worked = 1;
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
            /* Guest -> host: pull whatever the guest socket has queued. Drain the
             * whole guest rx (up to the window) per tick so the sender's window
             * reopens fully each round — a single small read would otherwise leave
             * the server parked on a near-full window and stall the transfer.
             *
             * Nothing new is pulled while a tail is still waiting for the host
             * socket: the guest's own ring is the buffer beyond that, and
             * letting it fill is how the seat is told to slow down. */
            if (c->guest_fd >= 0 && !c->connecting) {
                uint32_t guard = 64; /* cap a pathological loop, not a healthy drain */
                for (;;) {
                    ssize_t w = fwd_flush(c);
                    int32_t n;
                    if (w < 0) {
                        close(c->host_fd);
                        fwd_guest_close(e, c);
                        break;
                    }
                    if (w > 0) {
                        worked = 1;
                    }
                    if (c->pend_len != 0) {
                        break; /* host is full; the rest waits for POLLOUT */
                    }
                    n = pm_metal_net_ip_recv(c->guest_fd, c->pend, e->chunk);
                    if (n <= 0) {
                        if (n == -2) {
                            close(c->host_fd);
                            fwd_guest_close(e, c);
                        }
                        break;
                    }
                    c->pend_len = (uint32_t)n;
                    c->pend_off = 0;
                    worked = 1;
                    if (--guard == 0u) {
                        break;
                    }
                }
            }
        }
        if (worked) {
            wait_ns = 0;
        } else if (wait_ns == 0) {
            wait_ns = 200 * 1000;
        } else if (wait_ns < 50 * 1000 * 1000) {
            wait_ns *= 2;
            if (wait_ns > 50 * 1000 * 1000) {
                wait_ns = 50 * 1000 * 1000;
            }
        }
    }
    return NULL;
}

int32_t pm_metal_fwd_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    /* No mirrors, no table, nothing taken. A seat that mirrors nothing — every
     * firmware board, and a unix seat until something asks — carries this card
     * for the price of one null pointer. */
    s_ep = NULL;
    s_ep_cap = 0;
    s_mirror_used = 0;
    return 0;
}

/* Stop a mirror's thread and give back everything it was holding. */
static void fwd_ep_release(uint32_t i) {
    struct fwd_ep *e = s_ep[i];
    uint32_t j;
    if (e == NULL) {
        return;
    }
    if (e->running) {
        e->running = 0;
        if (e->active) {
            (void)pthread_join(e->th, NULL);
        }
        e->active = 0;
    }
    if (e->listen_fd >= 0) {
        close(e->listen_fd);
        e->listen_fd = -1;
    }
    for (j = 0; j < e->conn_cap; j++) {
        if (e->conn[j].host_fd >= 0) {
            close(e->conn[j].host_fd);
        }
        if (e->conn[j].guest_fd >= 0) {
            (void)pm_metal_net_ip_close(e->conn[j].guest_fd);
        }
        if (e->conn[j].pend != NULL) {
            pm_util_mem_free(s_arena, e->conn[j].pend);
        }
    }
    if (e->conn != NULL) {
        pm_util_mem_free(s_arena, e->conn);
    }
    if (e->pf != NULL) {
        pm_util_mem_free(s_arena, e->pf);
    }
    if (e->pfi != NULL) {
        pm_util_mem_free(s_arena, e->pfi);
    }
    if (e->hostbuf != NULL) {
        pm_util_mem_free(s_arena, e->hostbuf);
    }
    s_ep[i] = NULL;
    pm_util_mem_free(s_arena, e);
    if (s_mirror_used != 0u) {
        s_mirror_used--;
    }
}

/* poll() drives the pump; the prove's bounded pumping keeps tests deterministic. */
void pm_metal_fwd_deinit(void) {
    uint32_t i;
    for (i = 0; i < s_ep_cap; i++) {
        fwd_ep_release(i);
    }
    if (s_ep != NULL) {
        pm_util_mem_free(s_arena, s_ep);
    }
    s_ep = NULL;
    s_ep_cap = 0;
    s_mirror_used = 0;
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

/* Everything one mirror needs, in one place, before its thread exists. */
static struct fwd_ep *fwd_ep_new(uint16_t port) {
    uint32_t conns = pm_fwd_limit_conn.soft != 0u ? pm_fwd_limit_conn.soft : pm_fwd_limit_conn.dflt;
    uint32_t chunk = pm_fwd_limit_chunk.soft != 0u ? pm_fwd_limit_chunk.soft : pm_fwd_limit_chunk.dflt;
    struct fwd_ep *e = pm_util_mem_alloc(s_arena, sizeof(*e));
    uint32_t k;
    if (e == NULL) {
        return NULL;
    }
    memset(e, 0, sizeof(*e));
    e->port = port;
    e->listen_fd = -1;
    e->chunk = chunk;
    e->conn_cap = conns;
    e->conn = pm_util_mem_alloc(s_arena, (size_t)conns * sizeof(*e->conn));
    e->pf = pm_util_mem_alloc(s_arena, ((size_t)conns + 1u) * sizeof(*e->pf));
    e->pfi = pm_util_mem_alloc(s_arena, (size_t)conns * sizeof(*e->pfi));
    e->hostbuf = pm_util_mem_alloc(s_arena, (size_t)chunk);
    if (e->conn == NULL || e->pf == NULL || e->pfi == NULL || e->hostbuf == NULL) {
        goto give_up;
    }
    memset(e->conn, 0, (size_t)conns * sizeof(*e->conn));
    for (k = 0; k < conns; k++) {
        e->conn[k].host_fd = -1; /* free-slot sentinel; a fresh slot must be < 0 */
        e->conn[k].guest_fd = -1;
        /* One holding area per connection, for the tail a slow host socket
         * would not take. Taken with the mirror: the pump thread must never
         * reach for the arena, and a transfer must never wait on one. */
        e->conn[k].pend = pm_util_mem_alloc(s_arena, (size_t)chunk);
        if (e->conn[k].pend == NULL) {
            goto give_up;
        }
    }
    return e;

give_up:
    for (k = 0; k < conns; k++) {
        if (e->conn != NULL && e->conn[k].pend != NULL) {
            pm_util_mem_free(s_arena, e->conn[k].pend);
        }
    }
    if (e->conn != NULL) {
        pm_util_mem_free(s_arena, e->conn);
    }
    if (e->pf != NULL) {
        pm_util_mem_free(s_arena, e->pf);
    }
    if (e->pfi != NULL) {
        pm_util_mem_free(s_arena, e->pfi);
    }
    if (e->hostbuf != NULL) {
        pm_util_mem_free(s_arena, e->hostbuf);
    }
    pm_util_mem_free(s_arena, e);
    return NULL;
}

int32_t pm_metal_fwd_listen(uint16_t port) {
    uint32_t i;
    uint32_t slot;
    struct fwd_ep *e;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < s_ep_cap; i++) {
        if (s_ep[i] != NULL && s_ep[i]->running && s_ep[i]->port == port) {
            return (int32_t)i;
        }
    }
    for (slot = 0; slot < s_ep_cap; slot++) {
        if (s_ep[slot] == NULL) {
            break;
        }
    }
    if (slot == s_ep_cap) {
        struct fwd_ep **grown =
            pm_util_limits_grow(s_arena, s_ep, &s_ep_cap, (uint32_t)sizeof(*s_ep), &pm_fwd_limit_mirror);
        if (grown == NULL) {
            return -1;
        }
        s_ep = grown;
    }
    e = fwd_ep_new(port);
    if (e == NULL) {
        return -1;
    }
    e->listen_fd = fwd_open_listener(port);
    if (e->listen_fd < 0) {
        s_ep[slot] = e;
        s_mirror_used++;
        fwd_ep_release(slot);
        return -1;
    }
    s_ep[slot] = e;
    s_mirror_used++;
    e->running = 1;
    if (pthread_create(&e->th, NULL, fwd_pump, e) != 0) {
        e->running = 0;
        fwd_ep_release(slot);
        return -1;
    }
    e->active = 1;
    return (int32_t)slot;
}

uint32_t pm_metal_fwd_count(void) {
    uint32_t i;
    uint32_t n = 0;
    for (i = 0; i < s_ep_cap; i++) {
        if (s_ep[i] != NULL && s_ep[i]->running) {
            n++;
        }
    }
    return n;
}

int32_t pm_metal_fwd_status(int32_t id) {
    if (id < 0 || (uint32_t)id >= s_ep_cap || s_ep[id] == NULL) {
        return 0;
    }
    return s_ep[id]->running ? 1 : 0;
}

int32_t pm_metal_fwd_stop(int32_t id) {
    if (id < 0 || (uint32_t)id >= s_ep_cap) {
        return -1;
    }
    if (s_ep[id] == NULL) {
        return 0;
    }
    fwd_ep_release((uint32_t)id);
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
