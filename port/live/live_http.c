#include "live_http.h"

#include <stdio.h>

#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/asgi/__init__.h"
#include "services.h"

void uart_puts(const char *s);

void pm_metal_live_http(void)
{
    char line[48];
    int32_t rc;

    /* Boot already auto-starts on FW; LIVE loop only polls if still down. */
    if (!pm_metal_asgi_ready()) {
        rc = pm_metal_net_services_start();
        if (rc != 0) {
            snprintf(line, sizeof(line), "live services fail rc=%d\n", (int)rc);
            uart_puts(line);
        } else {
            uart_puts("live http\n");
        }
    }
    for (;;) {
        (void)pm_metal_async_run_poll();
    }
}
