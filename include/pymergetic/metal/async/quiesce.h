#ifndef PM_METAL_ASYNC_QUIESCE_H_
#define PM_METAL_ASYNC_QUIESCE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ask every runner to park at its next dispatch checkpoint. */
void pm_metal_async_quiesce_request(void);
/* 1 while a quiesce request is outstanding. */
int32_t pm_metal_async_quiesce_requested(void);
/* Checkpoint ack: runner ri has parked (no further dispatch). */
void pm_metal_async_quiesce_park_runner(uint32_t ri);
/* 1 once every started runner has parked since the last request. */
int32_t pm_metal_async_quiesce_all_parked(void);
/* Resume every parked runner. */
void pm_metal_async_quiesce_release(void);

#ifdef __cplusplus
}
#endif

#endif
