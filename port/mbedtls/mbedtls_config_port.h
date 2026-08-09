/*
 * Metal freestanding mbedtls config for net/tls.
 * Resolved as "mbedtls/mbedtls_config_port.h" with -I$(PORT_DIR).
 */
#ifndef PM_METAL_MBEDTLS_CONFIG_PORT_H_
#define PM_METAL_MBEDTLS_CONFIG_PORT_H_

#include <stddef.h>
#include <stdint.h>
#include <time.h>

void *pm_metal_mbedtls_calloc(size_t nmemb, size_t size);
void pm_metal_mbedtls_free(void *ptr);
time_t pm_metal_mbedtls_time(time_t *timer);
/* mbedtls_ms_time_t == int64_t under MBEDTLS_HAVE_TIME */
int64_t pm_metal_mbedtls_ms_time(void);

#define MBEDTLS_PLATFORM_TIME_MACRO pm_metal_mbedtls_time
#define MBEDTLS_PLATFORM_MS_TIME_ALT pm_metal_mbedtls_ms_time
/* Freestanding: no glibc explicit_bzero — supply mbedtls_platform_zeroize. */
#define MBEDTLS_PLATFORM_ZEROIZE_ALT
#define MBEDTLS_PLATFORM_GMTIME_R_ALT
/* Force software inet_pton (host headers may expose AF_INET6 without libc). */
#define MBEDTLS_TEST_SW_INET_PTON
#define MICROPY_MBEDTLS_CONFIG_BARE_METAL (1)

/*
 * UEFI seats compile with --target=*-windows (sets _MSC_VER). Without this,
 * mbedtls/platform_util.h takes the MSVC branch and #include <sal.h> (missing).
 */
#ifndef MBEDTLS_CHECK_RETURN
#if defined(__clang__) || defined(__GNUC__)
#define MBEDTLS_CHECK_RETURN __attribute__((__warn_unused_result__))
#else
#define MBEDTLS_CHECK_RETURN
#endif
#endif

#include "extmod/mbedtls/mbedtls_config_common.h"

/* PEM / WS accept key need base64 + PEM (not enabled in µPy common config). */
#ifndef MBEDTLS_BASE64_C
#define MBEDTLS_BASE64_C
#endif
#ifndef MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#endif
#ifndef MBEDTLS_PEM_WRITE_C
#define MBEDTLS_PEM_WRITE_C
#endif

#undef MBEDTLS_PLATFORM_STD_CALLOC
#undef MBEDTLS_PLATFORM_STD_FREE
#define MBEDTLS_PLATFORM_STD_CALLOC pm_metal_mbedtls_calloc
#define MBEDTLS_PLATFORM_STD_FREE pm_metal_mbedtls_free

#endif /* PM_METAL_MBEDTLS_CONFIG_PORT_H_ */
