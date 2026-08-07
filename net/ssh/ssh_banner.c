#include "pymergetic/metal/net/ssh.h"

#include <stdint.h>

#include "pymergetic/metal/net/tcp.h"

static int32_t g_sent;

static const char k_banner[] = "SSH-2.0-metal\r\n";

int32_t pm_metal_ssh_banner_send(void)
{
    if (!pm_metal_tcp_established()) {
        return -1;
    }
    if (g_sent) {
        return 0;
    }
    if (pm_metal_tcp_send(k_banner, (uint32_t)(sizeof(k_banner) - 1u)) != 0) {
        return -1;
    }
    g_sent = 1;
    return 0;
}

int32_t pm_metal_ssh_banner_sent(void)
{
    return g_sent;
}

void pm_metal_ssh_banner_reset(void)
{
    g_sent = 0;
}
