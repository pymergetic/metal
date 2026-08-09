#include <stddef.h>
#include <stdio.h>

#include "smoke_certs/server_crt.inc"
#include "smoke_certs/server_key.inc"

#include "pymergetic/metal/net/tls/__init__.h"

int32_t pm_metal_net_tls_load_smoke_server(void)
{
    char msg[48];
    extern void uart_puts(const char *s);
    snprintf(msg, sizeof(msg), "smoke len=%u\n",
             (unsigned)pm_metal_net_tls_smoke_server_crt_len);
    uart_puts(msg);
    if (pm_metal_net_tls_set_server_cert_pem((const uint8_t *)pm_metal_net_tls_smoke_server_crt,
                                            pm_metal_net_tls_smoke_server_crt_len) != 0) {
        uart_puts("smoke cert fail\n");
        return -1;
    }
    if (pm_metal_net_tls_set_server_key_pem((const uint8_t *)pm_metal_net_tls_smoke_server_key,
                                           pm_metal_net_tls_smoke_server_key_len) != 0) {
        uart_puts("smoke key fail\n");
        return -1;
    }
    uart_puts("smoke cert ok\n");
    return 0;
}
