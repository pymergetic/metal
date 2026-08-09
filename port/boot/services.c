#include "services.h"

#include <stdio.h>

#include "pymergetic/metal/net/asgi/__init__.h"
#include "pymergetic/metal/net/ssh/__init__.h"
#include "pymergetic/metal/net/tls/__init__.h"

void uart_puts(const char *s);

int32_t pm_metal_net_services_start(void)
{
    char line[64];
    int32_t rc;
    int32_t ok = 0;

    uart_puts("svc tls\n");
    if (pm_metal_net_tls_init() == 0) {
        uart_puts("svc smoke cert\n");
        (void)pm_metal_net_tls_load_smoke_server();
    } else {
        uart_puts("svc tls init fail\n");
    }

    uart_puts("svc asgi80\n");
    rc = pm_metal_asgi_init(80);
    if (rc != 0) {
        snprintf(line, sizeof(line), "httpd :80 fail rc=%d\n", (int)rc);
        uart_puts(line);
    } else {
        uart_puts("httpd :80\n");
        ok = 1;
    }

    uart_puts("svc asgi443\n");
    rc = pm_metal_asgi_init_tls(443);
    if (rc != 0) {
        snprintf(line, sizeof(line), "httpd :443 fail rc=%d\n", (int)rc);
        uart_puts(line);
    } else {
        uart_puts("httpd :443\n");
        ok = 1;
    }

    uart_puts("svc ssh\n");
    if (pm_metal_net_ssh_autoload() != 0) {
        uart_puts("sshd init fail\n");
    } else {
        pm_metal_net_ssh_banner_reset();
        if (pm_metal_net_ssh_listen(22) == 0u) {
            uart_puts("sshd :22 fail\n");
        } else {
            uart_puts("sshd :22\n");
        }
    }

    return ok ? 0 : -1;
}
