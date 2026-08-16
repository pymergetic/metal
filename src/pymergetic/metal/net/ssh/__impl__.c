/* pymergetic.metal.net.ssh — SSH-2.0 ident + KEXINIT on ip TCP. */
#include "pymergetic/metal/net/ssh/__exports__.h"

#include "pymergetic/metal/net/ip.h"

#include <string.h>

static const uint8_t s_ident[] = "SSH-2.0-metal\r\n";

static pm_util_mem_arena_t *s_arena;
static int32_t s_ls = -1;
static int32_t s_peer = -1;
static uint32_t s_kexinit_sent;

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t put_name(uint8_t *p, const char *s) {
    uint32_t n = 0;
    while (s[n] != 0) {
        n++;
    }
    put_be32(p, n);
    memcpy(p + 4, s, n);
    return 4u + n;
}

static int32_t send_kexinit(void) {
    uint8_t pkt[512];
    uint8_t payload[400];
    uint32_t n = 0;
    uint32_t i;
    uint32_t pad;
    uint32_t plen;
    payload[n++] = 20; /* SSH_MSG_KEXINIT */
    for (i = 0; i < 16; i++) {
        payload[n++] = (uint8_t)(0xa0u + i);
    }
    n += put_name(payload + n, "curve25519-sha256");
    n += put_name(payload + n, "ssh-ed25519");
    n += put_name(payload + n, "chacha20-poly1305@openssh.com");
    n += put_name(payload + n, "chacha20-poly1305@openssh.com");
    n += put_name(payload + n, "hmac-sha2-256");
    n += put_name(payload + n, "hmac-sha2-256");
    n += put_name(payload + n, "none");
    n += put_name(payload + n, "none");
    n += put_name(payload + n, "");
    n += put_name(payload + n, "");
    payload[n++] = 0; /* first_kex_packet_follows */
    put_be32(payload + n, 0);
    n += 4;
    pad = 8u - ((1u + n) % 8u);
    if (pad < 4u) {
        pad += 8u;
    }
    plen = 1u + n + pad;
    put_be32(pkt, plen);
    pkt[4] = (uint8_t)pad;
    memcpy(pkt + 5, payload, n);
    memset(pkt + 5 + n, 0, pad);
    if (pm_metal_net_ip_send(s_peer, pkt, 4u + plen) != (int32_t)(4u + plen)) {
        return -1;
    }
    s_kexinit_sent = 1;
    return 0;
}

int32_t pm_metal_net_ssh_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_ls = -1;
    s_peer = -1;
    s_kexinit_sent = 0;
    return 0;
}

void pm_metal_net_ssh_deinit(void) {
    if (s_peer >= 0) {
        (void)pm_metal_net_ip_close(s_peer);
        s_peer = -1;
    }
    if (s_ls >= 0) {
        (void)pm_metal_net_ip_close(s_ls);
        s_ls = -1;
    }
    s_kexinit_sent = 0;
    s_arena = NULL;
}

int32_t pm_metal_net_ssh_listen(uint32_t addr_be, uint16_t port) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_ls >= 0) {
        return 0;
    }
    s_ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (s_ls < 0 || pm_metal_net_ip_bind(s_ls, addr_be, port) != 0
        || pm_metal_net_ip_listen(s_ls, 1) != 0) {
        if (s_ls >= 0) {
            (void)pm_metal_net_ip_close(s_ls);
            s_ls = -1;
        }
        return -1;
    }
    return 0;
}

int32_t pm_metal_net_ssh_poll(void) {
    int32_t c;
    uint8_t buf[64];
    int32_t n;
    if (s_ls < 0) {
        return 0;
    }
    c = pm_metal_net_ip_accept(s_ls);
    if (c >= 0) {
        if (s_peer >= 0) {
            (void)pm_metal_net_ip_close(s_peer);
        }
        s_peer = c;
        s_kexinit_sent = 0;
        (void)pm_metal_net_ip_send(s_peer, s_ident, (uint32_t)(sizeof(s_ident) - 1u));
    }
    if (s_peer < 0 || s_kexinit_sent) {
        return 0;
    }
    n = pm_metal_net_ip_recv(s_peer, buf, sizeof(buf));
    if (n >= 4 && buf[0] == 'S' && buf[1] == 'S' && buf[2] == 'H' && buf[3] == '-') {
        (void)send_kexinit();
    }
    return 0;
}

const char *pm_metal_net_ssh_ident(void) {
    return (const char *)s_ident;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_init, pm_metal_net_ssh_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_deinit, pm_metal_net_ssh_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_listen, pm_metal_net_ssh_listen, int32_t(uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_poll, pm_metal_net_ssh_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_ident, pm_metal_net_ssh_ident, const char *(void));

PM_MOD_BOOT_C(pymergetic.metal.net.ssh, pm_metal_net_ssh_init, pm_metal_net_ssh_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ssh, pymergetic.metal.net.ip);
