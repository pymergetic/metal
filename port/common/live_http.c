#include "live_http.h"

#include "pymergetic/metal/asgi/__init__.h"
#include "pymergetic/metal/async/runner.h"

void uart_puts(const char *s);

void pm_metal_live_http(void)
{
    if (pm_metal_asgi_init(80) != 0) {
        uart_puts("live asgi fail\n");
        return;
    }
    uart_puts("live http\n");
    /* Scheduler only — pump drives ip + asgi/inspect. */
    for (;;) {
        (void)pm_metal_async_run_poll();
    }
}
