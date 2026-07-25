/* Metal MicroPython port config — freestanding; Metal owns I/O / tasks / FS. */
#include <stdint.h>
#include <stddef.h>

typedef intptr_t  mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long      mp_off_t;

#ifndef alloca
#define alloca(n) __builtin_alloca(n)
#endif

#define MICROPY_MPHALPORT_H "port/mphalport.h"

/* ---- baseline ---------------------------------------------------------- */
/* MINIMUM = smallest ROM feature set; we opt back in below. Not "we rebuild
 * µPy from scratch" — same upstream sources, just a lean CONFIG. */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

#define MICROPY_HW_BOARD_NAME  "metal"
#define MICROPY_HW_MCU_NAME    "metal"
#define MP_STATE_PORT          MP_STATE_VM
#define MICROPY_ALLOC_PATH_MAX (256)

/* ---- spike needs these on ---------------------------------------------- */
#define MICROPY_ENABLE_COMPILER        (1) /* shell python <script> / run_str */
#define MICROPY_ENABLE_GC              (1) /* always-on MAP blob heap */
#define MICROPY_PY_GC                  (1) /* gc module for diagnostics */
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1) /* import from FS / zip path */
#define MICROPY_PY_ASYNC_AWAIT         (1) /* await → Metal handles */
#define MICROPY_PY_SYS                 (1) /* sys module (MINIMUM leaves this off) */
#define MICROPY_PY_SYS_PATH            (1) /* search path for imports */

/* ---- Metal provides these — keep µPy copies off ------------------------ */
#define MICROPY_PY_THREAD         (0) /* Python task = Metal task; no GIL */
#define MICROPY_ENABLE_SCHEDULER  (0) /* Metal async, not mp_sched */
#define MICROPY_HELPER_REPL       (0) /* shell drives scripts, not µPy REPL */
#define MICROPY_VFS               (0) /* Metal FS + py_port_stubs import_stat/open */
#define MICROPY_MODULE_FROZEN_MPY (0) /* stdlib = signed zip, not frozen .mpy */
#define MICROPY_MODULE_FROZEN_STR (0) /* same — zip / HTTP seed later */

/* ---- bring-up off (link grease; re-enable when stubs/lib ready) -------- */
/* No libm / half-wired builtin modules until we wire them cleanly. */
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_NONE)
#define MICROPY_PY_BUILTINS_FLOAT   (0)
#define MICROPY_PY_BUILTINS_COMPLEX (0)
#define MICROPY_PY_MATH             (0)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_NONE)
#define MICROPY_PY_IO               (0) /* open stub only; full io later via Metal */
#define MICROPY_PY_STRUCT           (0)
#define MICROPY_PY_SYS_ARGV         (0)
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_TERSE)
