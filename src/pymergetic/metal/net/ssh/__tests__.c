/* pymergetic.metal.net.ssh — banner then KEXINIT after client ident. */
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ssh.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define SSH_PORT 2222

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.ssh test: %s\n", why);
    return 1;
}

int32_t pm_metal_net_ssh_tests(void) {
    const char *ident = pm_metal_net_ssh_ident();
    uint8_t buf[512];
    int32_t cl;
    int32_t n;
    uint32_t want;
    uint32_t plen;
    if (ident == NULL) {
        return fail("ident");
    }
    want = (uint32_t)strlen(ident);
    if (pm_metal_net_ssh_listen(LO4, SSH_PORT) != 0) {
        return fail("listen");
    }
    cl = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (cl < 0 || pm_metal_net_ip_connect(cl, LO4, SSH_PORT) != 1) {
        return fail("connect");
    }
    (void)pm_metal_net_ssh_poll();
    n = pm_metal_net_ip_recv(cl, buf, sizeof(buf));
    if (n != (int32_t)want || memcmp(buf, ident, want) != 0) {
        (void)pm_metal_net_ip_close(cl);
        return fail("banner");
    }
    if (pm_metal_net_ip_send(cl, (const uint8_t *)"SSH-2.0-test\r\n", 14) != 14) {
        (void)pm_metal_net_ip_close(cl);
        return fail("client ident");
    }
    (void)pm_metal_net_ssh_poll();
    n = pm_metal_net_ip_recv(cl, buf, sizeof(buf));
    (void)pm_metal_net_ip_close(cl);
    if (n < 6) {
        return fail("kexinit short");
    }
    plen = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    if (plen + 4u != (uint32_t)n) {
        return fail("kexinit length");
    }
    if (buf[5] != 20) {
        return fail("kexinit type");
    }
    return 0;
}
