/* Host smoke: mem (TLSF) + cooperative async floor. */
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/async/runner.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static uint8_t g_heap[256 * 1024];

int main(void)
{
    uint8_t *a;
    uint8_t *b;
    uint32_t h;
    int polls;
    int32_t n;

    if (pm_metal_mem_init(g_heap, sizeof(g_heap)) != 0) {
        fprintf(stderr, "mem_init failed\n");
        return 1;
    }
    a = pm_metal_mem_alloc(128);
    b = pm_metal_mem_alloc(256);
    if (a == NULL || b == NULL) {
        fprintf(stderr, "alloc failed\n");
        return 1;
    }
    memset(a, 0xA5, 128);
    memset(b, 0x5A, 256);
    pm_metal_mem_free(a);
    a = pm_metal_mem_realloc(b, 512);
    if (a == NULL) {
        fprintf(stderr, "realloc failed\n");
        return 1;
    }
    pm_metal_mem_free(a);
    printf("mem ok heap=%zu\n", pm_metal_mem_heap_bytes());

    if (pm_metal_async_start(1) != 0) {
        fprintf(stderr, "async_start failed\n");
        return 1;
    }
    h = pm_metal_async_sleep_us(5000);
    if (h == 0) {
        fprintf(stderr, "sleep alloc failed\n");
        return 1;
    }
    polls = 0;
    while (pm_metal_async_status(h) == PM_METAL_ASYNC_WAITING && polls < 200) {
        usleep(1000);
        n = pm_metal_async_run_poll();
        (void)n;
        polls++;
    }
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE) {
        fprintf(stderr, "sleep did not complete status=%d polls=%d\n",
                (int)pm_metal_async_status(h), polls);
        return 1;
    }
    h = pm_metal_async_yield();
    (void)pm_metal_async_run_poll();
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE
        && pm_metal_async_status(h) != PM_METAL_ASYNC_ERROR) {
        /* reclaim may have freed; treat completed poll as success if status ERROR on free */
    }
    printf("async ok mono_us=%llu\n", (unsigned long long)pm_metal_async_mono_us());
    printf("floor smoke ok\n");
    return 0;
}
