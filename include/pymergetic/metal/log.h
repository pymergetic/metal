#ifndef PM_METAL_LOG_H_
#define PM_METAL_LOG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thin sink until console ring lands — boards provide uart_puts. */
void pm_metal_log(const uint8_t *line);

#ifdef __cplusplus
}
#endif

#endif
