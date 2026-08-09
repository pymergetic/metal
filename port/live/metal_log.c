#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "pymergetic/metal/log.h"
#include "pymergetic/metal/console.h"

void uart_puts(const char *s);

void pm_metal_log(const uint8_t *line)
{
    size_t n;
    if (line == NULL) {
        return;
    }
    n = strlen((const char *)line);
    if (pm_metal_console_ready()) {
        (void)pm_metal_console_write(line, n);
        if (n == 0 || line[n - 1] != '\n') {
            (void)pm_metal_console_write((const uint8_t *)"\n", 1);
        }
        return;
    }
    uart_puts((const char *)line);
}
