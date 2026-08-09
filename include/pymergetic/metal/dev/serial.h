#ifndef PM_METAL_DEV_SERIAL_H_
#define PM_METAL_DEV_SERIAL_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pm_metal_dev_serial_write(const uint8_t *data, size_t n);

/* Console viewport sink → COM1 / board uart. */
void pm_metal_dev_serial_console_sink(const uint8_t *data, size_t n, void *user);

#ifdef __cplusplus
}
#endif

#endif
