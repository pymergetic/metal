/*
 * net.ssh — C impl (RS/Py are export faces).
 */
#include "pymergetic/metal/net/ssh/__init__.h"

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/net/tcp.h"

static int32_t g_banner_sent;
static uint32_t g_listen_port;

static const char k_banner[] = "SSH-2.0-metal\r\n";

int32_t pm_metal_net_ssh_available(void)
{
    return 0;
}

int32_t pm_metal_net_ssh_init(void)
{
    g_listen_port = 0;
    g_banner_sent = 0;
    return 0;
}

int32_t pm_metal_net_ssh_autoload(void)
{
    return pm_metal_net_ssh_init();
}

uint32_t pm_metal_net_ssh_listen(uint32_t port)
{
    (void)port;
    return 0;
}

void pm_metal_net_ssh_close(uint32_t s)
{
    (void)s;
}

int32_t pm_metal_net_ssh_poll(void)
{
    return -1;
}

int32_t pm_metal_net_ssh_served(void)
{
    return 0;
}

int32_t pm_metal_net_ssh_status(uint8_t *buf, uint32_t buf_len)
{
    static const char msg[] = "ssh: stub";
    uint32_t i;

    if (buf == NULL || buf_len == 0u) {
        return -1;
    }
    for (i = 0; i + 1u < buf_len && msg[i] != '\0'; i++) {
        buf[i] = (uint8_t)msg[i];
    }
    buf[i] = 0;
    return 0;
}

uint32_t pm_metal_net_ssh_listen_port(void)
{
    return g_listen_port;
}

int32_t pm_metal_net_ssh_hostkey_label(uint8_t *buf, uint32_t buf_len)
{
    if (buf != NULL && buf_len > 0u) {
        buf[0] = 0;
    }
    return -1;
}

int32_t pm_metal_net_ssh_client_exec(const char *host, uint16_t port,
    const char *user, const char *cmd, uint8_t *buf, uint32_t cap, uint32_t *len_out)
{
    (void)host;
    (void)port;
    (void)user;
    (void)cmd;
    (void)buf;
    (void)cap;
    if (len_out != NULL) {
        *len_out = 0;
    }
    return -1;
}

int32_t pm_metal_net_ssh_banner_send(void)
{
    if (!pm_metal_tcp_established()) {
        return -1;
    }
    if (g_banner_sent) {
        return 0;
    }
    if (pm_metal_tcp_send(k_banner, (uint32_t)(sizeof(k_banner) - 1u)) != 0) {
        return -1;
    }
    g_banner_sent = 1;
    return 0;
}

int32_t pm_metal_net_ssh_banner_sent(void)
{
    return g_banner_sent;
}

void pm_metal_net_ssh_banner_reset(void)
{
    g_banner_sent = 0;
}
