#include "wamr_smoke.h"

#include <stdbool.h>
#include <stdint.h>

#include "wasm_export.h"

void uart_puts(const char *s);

int pm_metal_wamr_smoke(void)
{
    /* System allocator → os_malloc → TLSF (floor must be up). */
    if (!wasm_runtime_init()) {
        uart_puts("wamr init fail\n");
        return -1;
    }
    uart_puts("wamr ok\n");
    return 0;
}
