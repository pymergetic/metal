#include "pymergetic/metal/net/ssh.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/dns.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/tcp.h"

static const char k_ident[] = "SSH-2.0-metal\r\n";

static int banner_has_ssh(const uint8_t *buf, uint32_t n)
{
    uint32_t i;

    if (buf == NULL || n < 7u) {
        return 0;
    }
    for (i = 0; i + 7u <= n; i++) {
        if (buf[i] == 'S' && buf[i + 1u] == 'S' && buf[i + 2u] == 'H' && buf[i + 3u] == '-' &&
            buf[i + 4u] == '2' && buf[i + 5u] == '.' && buf[i + 6u] == '0') {
            return 1;
        }
    }
    return 0;
}

int32_t pm_metal_ssh_client_ident_ip(uint32_t addr, uint16_t port, uint8_t *buf, uint32_t cap,
                                     uint32_t *len_out)
{
    uint32_t got = 0;
    uint32_t chunk;
    int i;
    int32_t rc;
    int32_t out = -1;
    int sent = 0;

    if (buf == NULL || len_out == NULL || cap < 16u || port == 0u || addr == 0u) {
        return -1;
    }
    *len_out = 0;

    for (i = 0; i < 32; i++) {
        rc = pm_metal_tcp_connect(addr, port);
        if (rc == 0) {
            break;
        }
        if (rc != -2) {
            goto done;
        }
        pm_metal_ip_poll();
    }
    if (rc != 0) {
        goto done;
    }

    for (i = 0; i < 40000 && !pm_metal_tcp_established(); i++) {
        pm_metal_ip_poll();
    }
    if (!pm_metal_tcp_established()) {
        out = -3;
        goto done;
    }

    for (i = 0; i < 40000; i++) {
        pm_metal_ip_poll();
        if (!sent) {
            rc = pm_metal_tcp_send(k_ident, (uint32_t)(sizeof(k_ident) - 1u));
            if (rc == 0) {
                sent = 1;
            } else if (rc != -2) {
                goto done;
            }
        }
        chunk = 0;
        rc = pm_metal_tcp_recv(buf + got, cap - got, &chunk);
        if (rc == 1 && chunk > 0u) {
            got += chunk;
            if (banner_has_ssh(buf, got)) {
                *len_out = got;
                out = 0;
                goto done;
            }
            if (got >= cap) {
                break;
            }
        }
    }
    if (banner_has_ssh(buf, got)) {
        *len_out = got;
        out = 0;
    } else {
        out = -2;
    }

done:
    pm_metal_tcp_abort();
    return out;
}

int32_t pm_metal_ssh_client_ident(const char *host, uint16_t port, uint8_t *buf, uint32_t cap,
                                  uint32_t *len_out)
{
    uint32_t addr = 0;

    if (host == NULL || buf == NULL || len_out == NULL || cap < 16u || port == 0u) {
        return -1;
    }
    *len_out = 0;
    if (pm_metal_dns_resolve(host, &addr) != 0 || addr == 0u) {
        return -1;
    }
    return pm_metal_ssh_client_ident_ip(addr, port, buf, cap, len_out);
}
