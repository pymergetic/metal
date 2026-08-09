#include "console_smoke.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/console.h"

void uart_puts(const char *s);
void uart_write(const char *s, size_t n);

static uint8_t g_console_ring[8 * 1024];

static void uart_sink(const uint8_t *data, size_t n, void *user)
{
    (void)user;
    uart_write((const char *)data, n);
}

int pm_metal_console_smoke(void)
{
    static const char pre[] = "console pre\n";
    static const char ok[] = "console ok\n";
    size_t n;

    if (pm_metal_console_init(g_console_ring, sizeof(g_console_ring)) != 0) {
        uart_puts("console init fail\n");
        return -1;
    }
    /* History before attach — must replay on attach. */
    n = pm_metal_console_write((const uint8_t *)pre, sizeof(pre) - 1u);
    if (n != sizeof(pre) - 1u) {
        uart_puts("console write fail\n");
        return -1;
    }
    if (pm_metal_console_attach(uart_sink, NULL) != 0) {
        uart_puts("console attach fail\n");
        return -1;
    }
    (void)pm_metal_console_write((const uint8_t *)ok, sizeof(ok) - 1u);
    return 0;
}
