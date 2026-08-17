/* Firmware fill for mbedtls: tracked calloc, hardware entropy, time(). */
#include "mbedtls/platform_time.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pm_cpu.h"

void *m_tracked_calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}

void m_tracked_free(void *ptr) {
    free(ptr);
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    static uint64_t s;
    size_t i;
    uint64_t t = 0;
    (void)data;
    t = pm_cpu_ticks();
    s = s * 6364136223846793005ull + 1u + t;
    if (output == NULL) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        s = s * 6364136223846793005ull + 1u;
        output[i] = (unsigned char)(s >> 33);
    }
    if (olen != NULL) {
        *olen = len;
    }
    return 0;
}

time_t time(time_t *t) {
    /* 2026-01-01 UTC — enough for TLS handshake; not a wall clock. */
    const time_t epoch = (time_t)1767225600;
    if (t != NULL) {
        *t = epoch;
    }
    return epoch;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)time(NULL) * 1000;
}
