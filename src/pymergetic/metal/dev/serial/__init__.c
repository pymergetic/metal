/* Serial device — unified over board uart_write. */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/dev/serial.h"

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_dev_serial_reg_load. */
static pm_metal_reg_export_t dev_serial_exports[] = {
    PM_METAL_REG_EXPORT(write),
    PM_METAL_REG_EXPORT(console_sink),
};
PM_METAL_REG_REF(dev_serial, write, 0);
PM_METAL_REG_REF(dev_serial, console_sink, 1);
PM_METAL_REG_MOD(dev_serial, "pymergetic.metal.dev.serial")

static int32_t dev_serial_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(dev_serial_write, (void *)pm_metal_dev_serial_write);
    pm_metal_reg_export_publish(dev_serial_console_sink, (void *)pm_metal_dev_serial_console_sink);
    return 0;
}
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
