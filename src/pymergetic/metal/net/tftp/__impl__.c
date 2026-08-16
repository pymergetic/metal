/* pymergetic.metal.net.tftp — RRQ → DATA block 1 on ip UDP. */
#include "pymergetic/metal/net/tftp/__exports__.h"

#include "pymergetic/metal/net/ip.h"

#include <string.h>

#define FILE_MAX 4
#define NAME_MAX 40
#define DATA_MAX 512

struct file {
    uint32_t used;
    char name[NAME_MAX];
    uint8_t data[DATA_MAX];
    uint16_t len;
};

static pm_util_mem_arena_t *s_arena;
static struct file s_file[FILE_MAX];
static int32_t s_fd = -1;

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
    if (name == NULL || name[0] == 0 || data == NULL || len == 0 || len > DATA_MAX) {
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

int32_t pm_metal_net_tftp_poll(void) {
    uint8_t buf[DATA_MAX + 4u];
    uint8_t out[DATA_MAX + 4u];
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
    if (n < 4 || buf[0] != 0 || buf[1] != 1) {
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
        out[0] = 0;
        out[1] = 3;
        out[2] = 0;
        out[3] = 1;
        memcpy(out + 4, s_file[i].data, s_file[i].len);
        (void)pm_metal_net_ip_sendto(s_fd, out, 4u + s_file[i].len, src, sport);
        return 0;
    }
    out[0] = 0;
    out[1] = 5;
    out[2] = 0;
    out[3] = 1;
    out[4] = 0;
    (void)pm_metal_net_ip_sendto(s_fd, out, 5, src, sport);
    return 0;
}

int32_t pm_metal_net_tftp_get(uint32_t server_be, uint16_t server_port, const char *name, uint8_t *out,
    uint32_t *out_len) {
    uint8_t q[NAME_MAX + 16u];
    uint8_t buf[DATA_MAX + 4u];
    int32_t fd;
    int32_t n;
    uint32_t i;
    uint32_t qn;
    if (name == NULL || out == NULL || out_len == NULL || s_arena == NULL) {
        return -1;
    }
    fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (fd < 0 || pm_metal_net_ip_bind(fd, 0x7f000001u, 6969) != 0) {
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
    (void)pm_metal_net_tftp_poll();
    n = pm_metal_net_ip_recvfrom(fd, buf, sizeof(buf), NULL, NULL);
    (void)pm_metal_net_ip_close(fd);
    if (n < 4 || buf[0] != 0 || buf[1] != 3) {
        return -1;
    }
    n -= 4;
    if ((uint32_t)n > *out_len) {
        n = (int32_t)*out_len;
    }
    memcpy(out, buf + 4, (uint32_t)n);
    *out_len = (uint32_t)n;
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
