#include "floor_smoke.h"

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/smp.h"
#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/dev/acpi/__init__.h"
#include "pymergetic/metal/net/pump/__init__.h"

void uart_puts(const char *s);

static uint8_t g_metal_heap[512 * 1024] __attribute__((aligned(16)));

static void uart_u32(uint32_t v)
{
    char b[11];
    int i = 0;
    int j;
    char t;

    if (v == 0) {
        uart_puts("0");
        return;
    }
    while (v > 0 && i < 10) {
        b[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    for (j = 0; j < i / 2; j++) {
        t = b[j];
        b[j] = b[i - 1 - j];
        b[i - 1 - j] = t;
    }
    b[i] = 0;
    uart_puts(b);
}

int pm_metal_floor_smoke(void)
{
    uint8_t *p;
    uint32_t h;
    uint32_t n_cpus;
    uint32_t online;
    uint32_t ri;
    uint32_t handles[8];
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

    if (pm_metal_dev_acpi_init() != 0) {
        uart_puts("floor acpi fail\n");
        return -1;
    }
    n_cpus = pm_metal_dev_acpi_cpu_count();
    if (n_cpus < 2u) {
        uart_puts("floor smp refuse n<2 cpus=");
        uart_u32(n_cpus);
        uart_puts(" rsdp=");
        uart_u32((uint32_t)pm_metal_dev_acpi_rsdp());
        uart_puts("\n");
        return -1;
    }
    if (pm_metal_async_start(n_cpus) != 0) {
        uart_puts("floor async_start fail\n");
        return -1;
    }
    if (pm_metal_async_n_runners() != n_cpus &&
        pm_metal_async_n_runners() < 2u) {
        uart_puts("floor runners mismatch\n");
        return -1;
    }
    if (pm_metal_smp_start() != 0) {
        uart_puts("floor smp_start fail\n");
        return -1;
    }
    online = pm_metal_smp_online_count();
    if (online < 2u || online != pm_metal_async_n_runners()) {
        uart_puts("floor smp online fail\n");
        return -1;
    }
    uart_puts("smp ");
    uart_u32(online);
    uart_puts(" online\n");

    pm_metal_net_pump_bind_async();

    /* AP alive: poll loop ticking off BSP. */
    {
        extern volatile uint32_t pm_metal_smp_poll_ticks[];
        uint32_t guard = 0;
        while (__atomic_load_n(&pm_metal_smp_poll_ticks[1], __ATOMIC_RELAXED) < 100u &&
               guard < 5000000u) {
            (void)pm_metal_async_run_poll();
            guard++;
        }
        if (__atomic_load_n(&pm_metal_smp_poll_ticks[1], __ATOMIC_RELAXED) < 100u) {
            uart_puts("floor ap poll fail\n");
            return -1;
        }
    }

    /* AP work proof: yield on every runner; APs drain their own. */
    for (ri = 0; ri < online && ri < 8u; ri++) {
        handles[ri] = pm_metal_async_create_task_on(0, ri);
        if (handles[ri] == 0u) {
            uart_puts("floor task_on fail\n");
            return -1;
        }
    }
    for (i = 0; i < 100000; i++) {
        uint32_t done = 0;
        (void)pm_metal_async_run_poll();
        for (ri = 0; ri < online && ri < 8u; ri++) {
            if (pm_metal_async_status(handles[ri]) == PM_METAL_ASYNC_DONE) {
                done++;
            }
        }
        if (done >= online || (online > 8u && done >= 8u)) {
            break;
        }
    }
    for (ri = 0; ri < online && ri < 8u; ri++) {
        if (pm_metal_async_status(handles[ri]) != PM_METAL_ASYNC_DONE) {
            uart_puts("floor ap work fail ri=");
            uart_u32(ri);
            uart_puts(" st=");
            uart_u32((uint32_t)pm_metal_async_status(handles[ri]));
            uart_puts("\n");
            return -1;
        }
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
    (void)pm_metal_async_create_task_on(h, 0);
    (void)pm_metal_async_run_poll();
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE) {
        uart_puts("floor yield fail\n");
        return -1;
    }
    uart_puts("floor ok\n");
    return 0;
}
