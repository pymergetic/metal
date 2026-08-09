#ifndef PM_METAL_MBEDTLS_PORT_H_
#define PM_METAL_MBEDTLS_PORT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional: bump wall clock after NTP (improves cert validity checks). */
void pm_metal_mbedtls_set_unix_time(uint32_t unix_secs);

#ifdef __cplusplus
}
#endif

#endif
