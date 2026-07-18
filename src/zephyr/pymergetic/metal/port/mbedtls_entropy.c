/*
 * mbedTLS platform hooks for Zephyr (entropy + ms time + buffer alloc).
 */
#include <mbedtls/entropy.h>

#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
#include <mbedtls/memory_buffer_alloc.h>

static unsigned char g_pm_metal_mbedtls_heap[128u * 1024u];
static int g_pm_metal_mbedtls_heap_ready;

void pm_metal_mbedtls_heap_init(void)
{
	if (g_pm_metal_mbedtls_heap_ready) {
		return;
	}
	mbedtls_memory_buffer_alloc_init(g_pm_metal_mbedtls_heap,
					   sizeof(g_pm_metal_mbedtls_heap));
	g_pm_metal_mbedtls_heap_ready = 1;
}
#endif

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
	(void)data;
	if (!output || !olen) {
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	}
	sys_rand_get(output, len);
	*olen = len;
	return 0;
}

#if defined(MBEDTLS_PLATFORM_MS_TIME_ALT)
#include <mbedtls/platform_time.h>

mbedtls_ms_time_t mbedtls_ms_time(void)
{
	return (mbedtls_ms_time_t)k_uptime_get();
}
#endif
