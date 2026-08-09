#ifndef PYMERGETIC_METAL_LOG_INIT_H_
#define PYMERGETIC_METAL_LOG_INIT_H_

#include <pymergetic/metal/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_METAL_LOG_STYLE_DEFAULT = 0,
    PM_METAL_LOG_STYLE_DIM = 1,
    PM_METAL_LOG_STYLE_OK = 2,
    PM_METAL_LOG_STYLE_WARN = 3,
    PM_METAL_LOG_STYLE_FAIL = 4,
    PM_METAL_LOG_STYLE_ACCENT = 5
} pm_metal_log_style_t;

void pm_metal_log_styled(pm_metal_log_style_t style, const uint8_t *line);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_LOG_INIT_H_ */
