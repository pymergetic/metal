#ifndef PYMERGETIC_METAL_PROCESS_MODE_H_
#define PYMERGETIC_METAL_PROCESS_MODE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_METAL_PROCESS_MODE_FG = 0,
    PM_METAL_PROCESS_MODE_BG = 1,
    PM_METAL_PROCESS_MODE_DAEMON = 2
} pm_metal_process_mode_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_PROCESS_MODE_H_ */
