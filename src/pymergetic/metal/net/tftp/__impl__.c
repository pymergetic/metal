/* pymergetic.metal.net.tftp — RRQ → DATA block 1 on ip UDP. */
#include "pymergetic/metal/net/tftp/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/ip.h"

#include <string.h>

#define FILE_MAX 4
#define NAME_MAX 40
#define DATA_MAX 512
#define FILE_BYTES 2048
#define TFTP_WAIT_US 3000000ull
#define TFTP_SPINS 200000u

struct file {
    uint32_t used;
    char name[NAME_MAX];
    uint8_t data[FILE_BYTES];
    uint16_t len;
};

/* One transfer at a time: a file is a run of blocks and the client's ACK is
 * what asks for the next one. */
struct xfer {
    uint32_t live;
    uint32_t fi;
    uint32_t addr;
    uint16_t port;
    uint16_t block;
};

static pm_util_mem_arena_t *s_arena;
static struct file s_file[FILE_MAX];
static struct xfer s_xfer;
static int32_t s_fd = -1;
static uint16_t s_cport = 49700;

static uint32_t name_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != 0 && b[i] != 0 && i < NAME_MAX; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return a[i] == 0 && b[i] == 0;
}

int32_t pm_metal_net_tftp_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_file, 0, sizeof(s_file));
    memset(&s_xfer, 0, sizeof(s_xfer));
    s_fd = -1;
    return 0;
}

void pm_metal_net_tftp_deinit(void) {
    if (s_fd >= 0) {
        (void)pm_metal_net_ip_close(s_fd);
        s_fd = -1;
    }
    s_arena = NULL;
}

int32_t pm_metal_net_tftp_add(const char *name, const uint8_t *data, uint16_t len) {
    uint32_t i;
    uint32_t n;
    if (name == NULL || name[0] == 0 || data == NULL || len == 0 || len > FILE_BYTES) {
        return -1;
    }
    for (i = 0; i < FILE_MAX; i++) {
        if (s_file[i].used) {
            continue;
        }
        n = 0;
        while (name[n] != 0 && n + 1u < NAME_MAX) {
            s_file[i].name[n] = name[n];
            n++;
        }
        s_file[i].name[n] = 0;
        memcpy(s_file[i].data, data, len);
        s_file[i].len = len;
        s_file[i].used = 1;
        return 0;
    }
    return -1;
}

int32_t pm_metal_net_tftp_listen(uint32_t addr_be, uint16_t port) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_fd >= 0) {
        return 0;
    }
    s_fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (s_fd < 0 || pm_metal_net_ip_bind(s_fd, addr_be, port) != 0) {
        if (s_fd >= 0) {
            (void)pm_metal_net_ip_close(s_fd);
            s_fd = -1;
        }
        return -1;
    }
    return 0;
}

/* Blocks are numbered from 1 and the last one is short — so a file whose length
 * is a whole number of blocks ends with an empty one. */
static uint32_t blocks_of(uint32_t len) {
    return len / DATA_MAX + 1u;
}

static void send_block(uint32_t src, uint16_t sport, uint32_t fi, uint16_t block) {
    uint8_t out[DATA_MAX + 4u];
    uint32_t off = (uint32_t)(block - 1u) * DATA_MAX;
    uint32_t dlen = s_file[fi].len > off ? s_file[fi].len - off : 0;
    if (dlen > DATA_MAX) {
        dlen = DATA_MAX;
    }
    out[0] = 0;
    out[1] = 3;
    out[2] = (uint8_t)(block >> 8);
    out[3] = (uint8_t)block;
    memcpy(out + 4, s_file[fi].data + off, dlen);
    (void)pm_metal_net_ip_sendto(s_fd, out, 4u + dlen, src, sport);
}

static void send_error(uint32_t src, uint16_t sport, uint8_t code) {
    uint8_t out[5];
    out[0] = 0;
    out[1] = 5;
    out[2] = 0;
    out[3] = code;
    out[4] = 0;
    (void)pm_metal_net_ip_sendto(s_fd, out, sizeof(out), src, sport);
}

int32_t pm_metal_net_tftp_poll(void) {
    uint8_t buf[DATA_MAX + 4u];
    uint32_t src = 0;
    uint16_t sport = 0;
    int32_t n;
    uint32_t i;
    uint32_t off;
    char name[NAME_MAX];
    if (s_fd < 0) {
        return 0;
    }
    n = pm_metal_net_ip_recvfrom(s_fd, buf, sizeof(buf), &src, &sport);
    if (n < 4 || buf[0] != 0) {
        return 0;
    }
    if (buf[1] == 4) { /* ACK: hand out the next block, or we are done */
        uint16_t acked = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
        if (!s_xfer.live || src != s_xfer.addr || sport != s_xfer.port || acked != s_xfer.block) {
            return 0;
        }
        if ((uint32_t)acked >= blocks_of(s_file[s_xfer.fi].len)) {
            s_xfer.live = 0;
            return 0;
        }
        s_xfer.block = (uint16_t)(acked + 1u);
        send_block(src, sport, s_xfer.fi, s_xfer.block);
        return 0;
    }
    if (buf[1] != 1) { /* only reads */
        return 0;
    }
    off = 0;
    while (2u + off < (uint32_t)n && buf[2 + off] != 0 && off + 1u < NAME_MAX) {
        name[off] = (char)buf[2 + off];
        off++;
    }
    name[off] = 0;
    for (i = 0; i < FILE_MAX; i++) {
        if (!s_file[i].used || !name_eq(s_file[i].name, name)) {
            continue;
        }
        s_xfer.live = 1;
        s_xfer.fi = i;
        s_xfer.addr = src;
        s_xfer.port = sport;
        s_xfer.block = 1;
        send_block(src, sport, i, 1);
        return 0;
    }
    send_error(src, sport, 1u);
    return 0;
}

/* Wait for the next packet of the transfer while driving the wire, and serve
 * our own socket so a get aimed at this box can be answered by it. */
static int32_t await_packet(int32_t fd, uint8_t *buf, uint32_t cap, uint32_t *src, uint16_t *sport) {
    uint64_t t0 = pm_metal_async_mono_us();
    uint32_t spins = 0;
    for (;;) {
        int32_t n = pm_metal_net_ip_recvfrom(fd, buf, cap, src, sport);
        if (n != 0) {
            return n;
        }
        (void)pm_metal_net_ip_pump();
        if (s_fd >= 0) {
            (void)pm_metal_net_tftp_poll();
        }
        if (pm_metal_async_mono_us() - t0 > TFTP_WAIT_US || ++spins > TFTP_SPINS) {
            return -1;
        }
    }
}

int32_t pm_metal_net_tftp_get(uint32_t server_be, uint16_t server_port, const char *name, uint8_t *out,
    uint32_t *out_len) {
    uint8_t q[NAME_MAX + 16u];
    uint8_t buf[DATA_MAX + 4u];
    int32_t fd;
    int32_t n;
    uint32_t i;
    uint32_t qn;
    uint32_t got = 0;
    uint32_t cap;
    uint16_t block = 1;
    uint16_t tid = server_port;
    uint32_t src = 0;
    uint16_t sport = 0;
    if (name == NULL || out == NULL || out_len == NULL || s_arena == NULL) {
        return -1;
    }
    cap = *out_len;
    fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    s_cport = s_cport < 49999u ? (uint16_t)(s_cport + 1u) : 49700u;
    if (fd < 0 || pm_metal_net_ip_bind(fd, 0, s_cport) != 0) {
        if (fd >= 0) {
            (void)pm_metal_net_ip_close(fd);
        }
        return -1;
    }
    q[0] = 0;
    q[1] = 1;
    i = 0;
    while (name[i] != 0 && i + 1u < NAME_MAX) {
        q[2 + i] = (uint8_t)name[i];
        i++;
    }
    q[2 + i] = 0;
    q[3 + i] = 'o';
    q[4 + i] = 'c';
    q[5 + i] = 't';
    q[6 + i] = 'e';
    q[7 + i] = 't';
    q[8 + i] = 0;
    qn = 9u + i;
    if (pm_metal_net_ip_sendto(fd, q, qn, server_be, server_port) < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    /* A file is a run of blocks, each acknowledged, and the server answers from
     * a transfer port of its own choosing — follow it. */
    for (;;) {
        uint32_t dlen;
        uint8_t ack[4];
        n = await_packet(fd, buf, sizeof(buf), &src, &sport);
        if (n < 4 || buf[0] != 0) {
            (void)pm_metal_net_ip_close(fd);
            return -1;
        }
        if (buf[1] == 5) { /* ERROR */
            (void)pm_metal_net_ip_close(fd);
            return -1;
        }
        if (buf[1] != 3 || (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]) != block) {
            continue;
        }
        if (block == 1u) {
            tid = sport;
        } else if (sport != tid) {
            continue;
        }
        dlen = (uint32_t)n - 4u;
        if (got + dlen > cap) {
            (void)pm_metal_net_ip_close(fd);
            return -1;
        }
        memcpy(out + got, buf + 4, dlen);
        got += dlen;
        ack[0] = 0;
        ack[1] = 4;
        ack[2] = buf[2];
        ack[3] = buf[3];
        (void)pm_metal_net_ip_sendto(fd, ack, sizeof(ack), src, tid);
        if (dlen < DATA_MAX) {
            break;
        }
        block++;
    }
    (void)pm_metal_net_ip_close(fd);
    *out_len = got;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.tftp, pm_metal_net_tftp_init, pm_metal_net_tftp_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.tftp, pm_metal_net_tftp_deinit, pm_metal_net_tftp_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.tftp, pm_metal_net_tftp_add, pm_metal_net_tftp_add, int32_t(const char *, const uint8_t *, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.tftp, pm_metal_net_tftp_listen, pm_metal_net_tftp_listen, int32_t(uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.tftp, pm_metal_net_tftp_poll, pm_metal_net_tftp_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.tftp, pm_metal_net_tftp_get, pm_metal_net_tftp_get, int32_t(uint32_t, uint16_t, const char *, uint8_t *, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.net.tftp, pm_metal_net_tftp_init, pm_metal_net_tftp_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.tftp, pymergetic.metal.net.ip);
