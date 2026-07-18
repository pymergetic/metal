/*
 * mbedTLS overrides for Zephyr qemu (freestanding — no POSIX net/FS).
 * Enabled via -DMBEDTLS_USER_CONFIG_FILE=... on the vendored tree.
 */
#ifndef PYMERGETIC_METAL_MBEDTLS_USER_CONFIG_H_
#define PYMERGETIC_METAL_MBEDTLS_USER_CONFIG_H_

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT

/* No wall clock on qemu until something sets it — skip X.509 notBefore/After. */
#undef MBEDTLS_HAVE_TIME_DATE

#undef MBEDTLS_TIMING_C
#undef MBEDTLS_NET_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C

/* Dedicated arena — avoid fighting Zephyr's shared k_malloc pool. */
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

/* Cert records can be large; keep TX modest. */
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

#endif /* PYMERGETIC_METAL_MBEDTLS_USER_CONFIG_H_ */
