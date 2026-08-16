/* Compile prove: firmware Metal image has GC and the µPy scheduler off. */
#include "mpconfig_firmware.h"

_Static_assert(MICROPY_ENABLE_GC == 0, "firmware Metal GC must be off");
_Static_assert(MICROPY_ENABLE_SCHEDULER == 0, "firmware Metal scheduler must be off");

int pm_metal_firmware_config_ok(void) {
    return 1;
}
