/*
 * C quiesce handshake (same contract as async/quiesce.rs).
 * Linked on unix where the Rust crate is not in the seat binary.
 */
#include "pymergetic/metal/async/quiesce.h"
#include "pymergetic/metal/async/runner.h"

#include <stdatomic.h>
#include <stdint.h>

enum { MAX_RUNNERS = 8 };

static atomic_int g_requested;
static atomic_int g_parked[MAX_RUNNERS];

void pm_metal_async_quiesce_request(void)
{
    uint32_t i;

    atomic_store(&g_requested, 1);
    for (i = 0; i < MAX_RUNNERS; i++) {
        atomic_store(&g_parked[i], 0);
    }
}

int32_t pm_metal_async_quiesce_requested(void)
{
    return atomic_load(&g_requested) ? 1 : 0;
}

void pm_metal_async_quiesce_park_runner(uint32_t ri)
{
    if (ri < MAX_RUNNERS && atomic_load(&g_requested)) {
        atomic_store(&g_parked[ri], 1);
    }
}

int32_t pm_metal_async_quiesce_all_parked(void)
{
    uint32_t n;
    uint32_t i;

    if (!atomic_load(&g_requested)) {
        return 0;
    }
    n = pm_metal_async_n_runners();
    if (n == 0) {
        return 1;
    }
    if (n > MAX_RUNNERS) {
        n = MAX_RUNNERS;
    }
    for (i = 0; i < n; i++) {
        if (!atomic_load(&g_parked[i])) {
            return 0;
        }
    }
    return 1;
}

void pm_metal_async_quiesce_release(void)
{
    uint32_t i;

    atomic_store(&g_requested, 0);
    for (i = 0; i < MAX_RUNNERS; i++) {
        atomic_store(&g_parked[i], 0);
    }
}
