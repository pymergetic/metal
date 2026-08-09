#include "floor_smoke.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/fs.h"
#include "pymergetic/metal/pack/mod_packs.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/smp.h"
#include "pymergetic/metal/async/prio.h"
#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/dev/acpi/__init__.h"
#include "pymergetic/metal/net/pump/__init__.h"

#include "metal_heap_buf.h"

void uart_puts(const char *s);

static uint8_t g_metal_heap[PM_METAL_PORT_HEAP_BYTES] __attribute__((aligned(4096)));

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
    /* Cold reg ledger pilot rows (inspect only). */
    {
        extern int32_t pm_metal_reg_ledger_seed_pilot(void);
        (void)pm_metal_reg_ledger_seed_pilot();
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

    /* H/M/L ready classes drain (shared schedule). */
    {
        uint32_t hi = pm_metal_async_create_task_prio(0, 0, PM_METAL_ASYNC_PRIO_HIGH);
        uint32_t lo = pm_metal_async_create_task_prio(0, 0, PM_METAL_ASYNC_PRIO_LOW);
        if (hi == 0u || lo == 0u) {
            uart_puts("floor prio alloc fail\n");
            return -1;
        }
        (void)pm_metal_async_run_poll();
        if (pm_metal_async_status(hi) != PM_METAL_ASYNC_DONE ||
            pm_metal_async_status(lo) != PM_METAL_ASYNC_DONE ||
            pm_metal_async_get_prio(hi) != PM_METAL_ASYNC_PRIO_HIGH) {
            uart_puts("floor prio drain fail\n");
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
    uart_puts("floor yield ok\n");

    /* Metal packs via fs_wasmmod (build-generated MPWP). Engine self-pack is wasmmod's job. */
    {
        static const uint8_t metal_httpd[] = "/mods/pymergetic.metal/httpd.json";
        static const uint8_t inspect_idx[] =
            "/mods/pymergetic.metal.inspect/www/inspect/index.html";
        uint8_t buf[128];
        uint32_t ah;
        uint32_t n;

        uart_puts("floor pack mount\n");
        if (pm_metal_mod_packs_mount_all() != 0) {
            uart_puts("floor pack mount fail\n");
            return -1;
        }
        uart_puts("floor pack mounted\n");
        memset(buf, 0, sizeof(buf));
        ah = pm_metal_fs_read_async(metal_httpd, buf, sizeof(buf) - 1u);
        n = pm_metal_async_result_u32(ah);
        pm_metal_async_coro_close(ah);
        if (n < 10u || buf[0] != '{' ||
            (memcmp(buf, "{\"static\"", 9) != 0 &&
             memcmp(buf, "{\n  \"static\"", 12) != 0)) {
            uart_puts("floor metal pack read fail\n");
            return -1;
        }
        memset(buf, 0, sizeof(buf));
        ah = pm_metal_fs_read_async(inspect_idx, buf, sizeof(buf) - 1u);
        n = pm_metal_async_result_u32(ah);
        pm_metal_async_coro_close(ah);
        if (n < 8u) {
            uart_puts("floor inspect pack read fail\n");
            return -1;
        }
        uart_puts("vfs ok\n");
    }

    uart_puts("floor ok\n");
    return 0;
}
