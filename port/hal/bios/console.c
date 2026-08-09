#include "api.h"

#include <stddef.h>

void uart_init(void);
void uart_puts(const char *s);
void uart_write(const char *s, size_t n);

void pm_metal_hal_console_init(void)
{
    uart_init();
}

void pm_metal_hal_console_puts(const char *s)
{
    uart_puts(s);
    uart_puts("\n");
}

void pm_metal_hal_console_write(const char *s, size_t n)
{
    uart_write(s, n);
}

const char *pm_metal_hal_cpu_label(void)
{
    return "x86_64";
}

int pm_metal_hal_mem_claim(uint8_t **base, size_t *bytes)
{
    /* Firmware product boot uses board static/claim_arena; not this HAL. */
    (void)base;
    (void)bytes;
    return -1;
}

void pm_metal_hal_mem_set_budget(size_t bytes)
{
    (void)bytes;
}

size_t pm_metal_hal_mem_budget(void)
{
    return 0;
}

int pm_metal_hal_is_sim(void)
{
    return 0;
}
