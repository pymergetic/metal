/* Serial log sink for freestanding boards (until console ring). */
#include "pymergetic/metal/log.h"

#include <stddef.h>

void uart_puts(const char *s);

void pm_metal_log(const uint8_t *line)
{
    if (line == NULL) {
        return;
    }
    uart_puts((const char *)line);
}
