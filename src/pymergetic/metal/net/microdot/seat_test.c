/*
 * Co-located reg seat test for frozen net.microdot.
 * Resolve via frozen finder (dotted µPy import of CORE is flaky on this port).
 */
#include "py/frozenmod.h"
#include "py/obj.h"

#include <stddef.h>
#include <stdint.h>

extern void uart_puts(const char *s);

int32_t pm_metal_net_microdot_seat_test(void)
{
    mp_import_stat_t st = mp_find_frozen_module("pymergetic/metal/net/microdot", NULL, NULL);
    if (st != MP_IMPORT_STAT_DIR && st != MP_IMPORT_STAT_FILE) {
        st = mp_find_frozen_module("pymergetic/metal/net/microdot/__init__.py", NULL, NULL);
        if (st != MP_IMPORT_STAT_FILE) {
            return -1;
        }
    }
    uart_puts("microdot ok\n");
    return 0;
}
