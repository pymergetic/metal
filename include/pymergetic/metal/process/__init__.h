#ifndef PYMERGETIC_METAL_PROCESS_H_
#define PYMERGETIC_METAL_PROCESS_H_

#include <stdint.h>

#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/prio.h>
#include <pymergetic/metal/process/mode.h>
#include <pymergetic/metal/process/table.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Spawn async task and crown as process. Returns pid or 0. */
uint32_t pm_metal_process_spawn(pm_metal_async_step_fn_t step, uint32_t state_bytes,
                                pm_metal_async_prio_t prio, pm_metal_process_mode_t mode,
                                const char *tag, pm_metal_process_teardown_fn teardown,
                                void *teardown_user);

/** Promote existing async handle (0 = table-only daemon face). Returns pid or 0. */
uint32_t pm_metal_process_crown(uint32_t async_handle, pm_metal_process_mode_t mode,
                                const char *tag, pm_metal_process_teardown_fn teardown,
                                void *teardown_user);

/** Quit pid (0 = current). Returns 0 ok, -1 missing. */
int32_t pm_metal_process_quit(uint32_t pid, int32_t code);

uint32_t pm_metal_process_current(void);

/** Fill infos[0..max); returns count. */
uint32_t pm_metal_process_list(pm_metal_process_info_t *infos, uint32_t max);

/** Quit every live process (used by unboot). Returns how many quit. */
uint32_t pm_metal_process_quit_all(int32_t code);

int32_t pm_metal_process_shutting_down(void);
void pm_metal_process_set_shutting_down(int32_t on);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_PROCESS_H_ */
