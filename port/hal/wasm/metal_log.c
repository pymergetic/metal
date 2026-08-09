/* Browser log sink — HAL console (no metal.console ring). */
#include <stdint.h>

#include "api.h"
#include "pymergetic/metal/log.h"

void pm_metal_log(const uint8_t *line)
{
    if (line == NULL) {
        return;
    }
    pm_metal_hal_console_puts((const char *)line);
}
