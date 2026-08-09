#include "api.h"

#include <stdio.h>
#include <string.h>

void pm_metal_hal_console_init(void)
{
    /* Emscripten stdout → REPL panel. */
}

void pm_metal_hal_console_puts(const char *s)
{
    if (s == NULL) {
        return;
    }
    fputs(s, stdout);
    if (s[0] == '\0' || s[strlen(s) - 1] != '\n') {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

void pm_metal_hal_console_write(const char *s, size_t n)
{
    if (s == NULL || n == 0) {
        return;
    }
    fwrite(s, 1, n, stdout);
    fflush(stdout);
}

const char *pm_metal_hal_cpu_label(void)
{
    return "wasm32";
}

int pm_metal_hal_is_sim(void)
{
    return 1;
}
