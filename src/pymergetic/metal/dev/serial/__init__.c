/* Serial device — unified over board uart_write. */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/dev/serial.h"

void uart_write(const char *s, size_t n);

void pm_metal_dev_serial_write(const uint8_t *data, size_t n)
{
    if (data == NULL || n == 0) {
        return;
    }
    uart_write((const char *)data, n);
}

void pm_metal_dev_serial_console_sink(const uint8_t *data, size_t n, void *user)
{
    (void)user;
    pm_metal_dev_serial_write(data, n);
}
