#include "services.h"

#include "pymergetic/metal/net/asgi/__init__.h"
#include "pymergetic/metal/net/ssh/__init__.h"
#include "pymergetic/metal/net/tls/__init__.h"

/*
 * Start product listeners. Silent on purpose — boot.tree / REPL print status.
 * Returns 0 if at least ASGI :80 started.
 */
int32_t pm_metal_net_services_start(void)
{
    int32_t ok = 0;

    if (pm_metal_net_tls_init() == 0) {
        (void)pm_metal_net_tls_load_smoke_server();
    }

    if (pm_metal_asgi_init(80) == 0) {
        ok = 1;
    }
    if (pm_metal_asgi_init_tls(443) == 0) {
        ok = 1;
    }

    if (pm_metal_net_ssh_init() == 0) {
        pm_metal_net_ssh_banner_reset();
        (void)pm_metal_net_ssh_listen(22);
    }

    return ok ? 0 : -1;
}
