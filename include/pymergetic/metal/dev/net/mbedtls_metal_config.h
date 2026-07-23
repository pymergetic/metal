/*
 * mbedTLS profile for Metal EFI HTTP/TLS client (TLS 1.2+, SNI, verify optional).
 */
#ifndef PYMERGETIC_METAL_DEV_NET_MBEDTLS_METAL_CONFIG_H_
#define PYMERGETIC_METAL_DEV_NET_MBEDTLS_METAL_CONFIG_H_

#include <stddef.h>
#include <stdarg.h>

int mbedtls_metal_snprintf(char *s, size_t n, const char *fmt, ...);
int mbedtls_metal_vsnprintf(char *s, size_t n, const char *fmt, va_list ap);

#define MBEDTLS_HAVE_ASM
#define MBEDTLS_NO_UDBL_DIVISION
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_ZEROIZE_ALT
#define MBEDTLS_TEST_SW_INET_PTON

/* EFI clangd uses -target x86_64-unknown-windows (_MSC_VER); skip MSVC sal.h. */
#if !defined(MBEDTLS_CHECK_RETURN)
#define MBEDTLS_CHECK_RETURN __attribute__((__warn_unused_result__))
#endif

#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_GCM_C
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO   mbedtls_metal_snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO  mbedtls_metal_vsnprintf
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SSL_CIPHERSUITES_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hook mbedTLS PLATFORM_MEMORY calloc/free to the Metal heap.
 * Must run before any x509/pk parse (trust) or TLS — idempotent.
 */
void pm_metal_mbedtls_runtime_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_MBEDTLS_METAL_CONFIG_H_ */
