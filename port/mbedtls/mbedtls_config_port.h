/*
 * Firmware mbedtls — same `pymergetic.metal.net.tls` / wg bignum as unix.
 * Bare-metal entropy + calloc; no /dev/urandom, no AESNI (UEFI -mno-sse).
 */
#ifndef MICROPY_INCLUDED_MBEDTLS_CONFIG_H
#define MICROPY_INCLUDED_MBEDTLS_CONFIG_H

/* UEFI clang --target=*-windows* sets _MSC_VER; mbedtls then wants sal.h.
 * This image is freestanding, not MSVC. */
#ifdef PM_METAL_UEFI
#ifdef _MSC_VER
#undef _MSC_VER
#endif
#ifdef _WIN32
#undef _WIN32
#endif
#ifdef WIN32
#undef WIN32
#endif
#endif

#ifndef MICROPY_PY_SSL_DTLS
#define MICROPY_PY_SSL_DTLS (0)
#endif
#define MICROPY_MBEDTLS_CONFIG_BARE_METAL (1)

#include "extmod/mbedtls/mbedtls_config_common.h"

#define MBEDTLS_CIPHER_MODE_CTR

/* libc has time() (fixed epoch). Skip gmtime_r / cert calendar. */
#undef MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_PLATFORM_MS_TIME_ALT

#endif /* MICROPY_INCLUDED_MBEDTLS_CONFIG_H */
