/*
 * Browser dev.serial — console sink writes to host stdout.
 */
#include "pymergetic/metal/dev/serial.h"

#include <stdio.h>

void pm_metal_dev_serial_write(const uint8_t *data, size_t n)
{
    if (!data || n == 0u) {
        return;
    }
    (void)fwrite(data, 1, n, stdout);
    (void)fflush(stdout);
}

void pm_metal_dev_serial_console_sink(const uint8_t *data, size_t n, void *user)
{
    (void)user;
    pm_metal_dev_serial_write(data, n);
}
