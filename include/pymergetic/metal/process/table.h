#ifndef PYMERGETIC_METAL_PROCESS_TABLE_H_
#define PYMERGETIC_METAL_PROCESS_TABLE_H_

#include <stdint.h>

#include <pymergetic/metal/process/mode.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PM_METAL_PROCESS_MAX
#define PM_METAL_PROCESS_MAX 32
#endif

#ifndef PM_METAL_PROCESS_TAG_MAX
#define PM_METAL_PROCESS_TAG_MAX 32
#endif

typedef void (*pm_metal_process_teardown_fn)(uint32_t pid, void *user);

typedef struct pm_metal_process_info {
    uint32_t pid;
    uint32_t async_handle;
    pm_metal_process_mode_t mode;
    int32_t exit_code;
    char tag[PM_METAL_PROCESS_TAG_MAX];
} pm_metal_process_info_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_PROCESS_TABLE_H_ */
