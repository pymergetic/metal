/*
 * mbedTLS 3.6 profile for Metal net.ip.ssl (TLS 1.2 client+server, SNI).
 * Selected via -DMBEDTLS_CONFIG_FILE=\"mbedtls_metal_config.h\".
 */
#ifndef PM_METAL_NET_IP_SSL_MBEDTLS_METAL_CONFIG_H_
#define PM_METAL_NET_IP_SSL_MBEDTLS_METAL_CONFIG_H_

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

#if !defined(MBEDTLS_CHECK_RETURN)
#define MBEDTLS_CHECK_RETURN __attribute__((__warn_unused_result__))
#endif

#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_GCM_C
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO  mbedtls_metal_snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO mbedtls_metal_vsnprintf
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C

#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_ENTROPY_MAX_SOURCES 2
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 16384

void pm_metal_net_ip_ssl_mbedtls_runtime_init(void);

#endif /* PM_METAL_NET_IP_SSL_MBEDTLS_METAL_CONFIG_H_ */
