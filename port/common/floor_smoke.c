#include "floor_smoke.h"

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/board_time.h"

void uart_puts(const char *s);

static uint8_t g_metal_heap[512 * 1024] __attribute__((aligned(16)));

int pm_metal_floor_smoke(void)
{
    uint8_t *p;
    uint32_t h;
    int i;

    if (pm_metal_mem_init(g_metal_heap, sizeof(g_metal_heap)) != 0) {
        uart_puts("floor mem_init fail\n");
        return -1;
    }
    p = pm_metal_mem_alloc(64);
    if (p == NULL) {
        uart_puts("floor alloc fail\n");
        return -1;
    }
    pm_metal_mem_free(p);

    if (pm_metal_async_start(1) != 0) {
        uart_puts("floor async_start fail\n");
        return -1;
    }
    h = pm_metal_async_sleep_us(2000);
    if (h == 0) {
        uart_puts("floor sleep fail\n");
        return -1;
    }
    for (i = 0; i < 8; i++) {
        pm_metal_board_time_advance_us(500);
        (void)pm_metal_async_run_poll();
        if (pm_metal_async_status(h) == PM_METAL_ASYNC_DONE) {
            break;
        }
    }
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE) {
        uart_puts("floor sleep timeout\n");
        return -1;
    }
    h = pm_metal_async_yield();
    (void)pm_metal_async_run_poll();
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE) {
        uart_puts("floor yield fail\n");
        return -1;
    }
    uart_puts("floor ok\n");
    return 0;
}
