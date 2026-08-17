/* pymergetic.metal.net.ntp — SNTP (mode 3 query, mode 4 reply) on ip UDP. */
#include "pymergetic/metal/net/ntp/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/ip.h"

#include <string.h>

#define NTP_LEN 48u
#define NTP_UNIX 0x83aa7e80u
#define NTP_WAIT_US 2000000ull
#define NTP_SPINS 200000u

static pm_util_mem_arena_t *s_arena;
static int32_t s_fd = -1;
static uint32_t s_now = NTP_UNIX + 1u;
static uint16_t s_cport = 49600;

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int32_t pm_metal_net_ntp_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_fd = -1;
    return 0;
}

void pm_metal_net_ntp_deinit(void) {
    if (s_fd >= 0) {
        (void)pm_metal_net_ip_close(s_fd);
        s_fd = -1;
    }
    s_arena = NULL;
}

int32_t pm_metal_net_ntp_listen(uint32_t addr_be, uint16_t port) {
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

int32_t pm_metal_net_ntp_poll(void) {
    uint8_t buf[NTP_LEN];
    uint32_t src = 0;
    uint16_t sport = 0;
    int32_t n;
    if (s_fd < 0) {
        return 0;
    }
    n = pm_metal_net_ip_recvfrom(s_fd, buf, sizeof(buf), &src, &sport);
    if (n < (int32_t)NTP_LEN) {
        return 0;
    }
    if ((buf[0] & 0x07u) != 3u) {
        return 0;
    }
    buf[0] = (uint8_t)((buf[0] & 0xf8u) | 4u);
    buf[1] = 1;
    memcpy(buf + 24, buf + 40, 8); /* their transmit becomes our origin */
    put_be32(buf + 32, s_now);
    put_be32(buf + 36, 0);
    put_be32(buf + 40, s_now);
    put_be32(buf + 44, 0);
    (void)pm_metal_net_ip_sendto(s_fd, buf, NTP_LEN, src, sport);
    return 0;
}

/* Wait for the reply while driving the wire, and serve our own socket so a
 * query aimed at this box can be answered by it. */
static int32_t await_reply(int32_t fd, uint8_t *buf, uint32_t cap) {
    uint64_t t0 = pm_metal_async_mono_us();
    uint32_t spins = 0;
    for (;;) {
        int32_t n = pm_metal_net_ip_recvfrom(fd, buf, cap, NULL, NULL);
        if (n != 0) {
            return n;
        }
        (void)pm_metal_net_ip_pump();
        if (s_fd >= 0) {
            (void)pm_metal_net_ntp_poll();
        }
        if (pm_metal_async_mono_us() - t0 > NTP_WAIT_US || ++spins > NTP_SPINS) {
            return -1;
        }
    }
}

int32_t pm_metal_net_ntp_query(uint32_t server_be, uint16_t server_port, uint32_t *unix_sec) {
    uint8_t q[NTP_LEN];
    uint8_t buf[NTP_LEN];
    int32_t fd;
    int32_t n;
    uint32_t xmt;
    if (unix_sec == NULL || s_arena == NULL) {
        return -1;
    }
    fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    s_cport = s_cport < 49999u ? (uint16_t)(s_cport + 1u) : 49600u;
    if (fd < 0 || pm_metal_net_ip_bind(fd, 0, s_cport) != 0) {
        if (fd >= 0) {
            (void)pm_metal_net_ip_close(fd);
        }
        return -1;
    }
    memset(q, 0, sizeof(q));
    q[0] = 0x1b; /* VN=3, mode=3 */
    /* Our transmit timestamp; the server echoes it as origin and that is what
     * tells this reply apart from a stray datagram. */
    xmt = (uint32_t)(pm_metal_async_mono_us() & 0xffffffffu) | 1u;
    put_be32(q + 40, xmt);
    if (pm_metal_net_ip_sendto(fd, q, NTP_LEN, server_be, server_port) < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    n = await_reply(fd, buf, sizeof(buf));
    (void)pm_metal_net_ip_close(fd);
    if (n < (int32_t)NTP_LEN || (buf[0] & 0x07u) != 4u) {
        return -1;
    }
    if (get_be32(buf + 24) != xmt) {
        return -1;
    }
    if (buf[1] == 0 || get_be32(buf + 40) < NTP_UNIX) {
        return -1;
    }
    *unix_sec = get_be32(buf + 40) - NTP_UNIX;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.ntp, pm_metal_net_ntp_init, pm_metal_net_ntp_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ntp, pm_metal_net_ntp_deinit, pm_metal_net_ntp_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ntp, pm_metal_net_ntp_listen, pm_metal_net_ntp_listen, int32_t(uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ntp, pm_metal_net_ntp_poll, pm_metal_net_ntp_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ntp, pm_metal_net_ntp_query, pm_metal_net_ntp_query, int32_t(uint32_t, uint16_t, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.net.ntp, pm_metal_net_ntp_init, pm_metal_net_ntp_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ntp, pymergetic.metal.net.ip);
