/*
 * Browser net.ssh — same C ABI; no TCP listen/client in the browser seat.
 */
#include "pymergetic/metal/net/ssh/__init__.h"

#include <stddef.h>
#include <stdint.h>

int32_t pm_metal_net_ssh_available(void)
{
    return 0;
}

int32_t pm_metal_net_ssh_init(void)
{
    return -1;
}

int32_t pm_metal_net_ssh_autoload(void)
{
    return -1;
}

uint32_t pm_metal_net_ssh_listen(uint32_t port)
{
    (void)port;
    return 0u;
}

void pm_metal_net_ssh_release(void) {}

void pm_metal_net_ssh_close(uint32_t s)
{
    (void)s;
}

int32_t pm_metal_net_ssh_poll(void)
{
    return 0;
}

int32_t pm_metal_net_ssh_served(void)
{
    return 0;
}

int32_t pm_metal_net_ssh_status(uint8_t *buf, uint32_t buf_len)
{
    if (buf != NULL && buf_len > 0u) {
        buf[0] = '\0';
    }
    return -1;
}

uint32_t pm_metal_net_ssh_listen_port(void)
{
    return 0u;
}

int32_t pm_metal_net_ssh_hostkey_label(uint8_t *buf, uint32_t buf_len)
{
    if (buf != NULL && buf_len > 0u) {
        buf[0] = '\0';
    }
    return -1;
}

int32_t pm_metal_net_ssh_client_exec(const char *host, uint16_t port, const char *user,
                                     const char *cmd, uint8_t *buf, uint32_t cap, uint32_t *len_out)
{
    (void)host;
    (void)port;
    (void)user;
    (void)cmd;
    (void)buf;
    (void)cap;
    if (len_out) {
        *len_out = 0u;
    }
    return -1;
}

int32_t pm_metal_net_ssh_banner_send(void)
{
    return -1;
}

int32_t pm_metal_net_ssh_banner_sent(void)
{
    return 0;
}

void pm_metal_net_ssh_banner_reset(void) {}

int32_t pm_metal_net_ssh_bind_reg(void)
{
    return -1;
}
