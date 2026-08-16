/*
 * Firmware µPy seat (BIOS/UEFI cake). GC off, scheduler off.
 * Included as mpconfigport.h from the port/ make directory.
 */
#include <stdint.h>

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#ifndef MICROPY_MODULE_BUILTIN_INIT
#define MICROPY_MODULE_BUILTIN_INIT (1)
#endif
#ifndef MICROPY_MODULE_BUILTIN_SUBPACKAGES
#define MICROPY_MODULE_BUILTIN_SUBPACKAGES (1)
#endif
#define MICROPY_HELPER_REPL (0)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_NONE)
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_NONE)
#define MICROPY_ALLOC_PATH_MAX (256)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_NORMAL)
/* UEFI: naked Win64 nlr_push in py/nlrx64.c (clang %rcx = nlr_buf). */
#define MICROPY_USE_INTERNAL_PRINTF (1)
#define MICROPY_USE_INTERNAL_ERRNO (1)
#define MICROPY_PY_BUILTINS (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (0)
/* UEFI COFF: mp_store_attr on ROM builtins misses; mp_import_name must
 * call the pack hook without the override-dict pointer compare. */
#define mp_builtin___import__ mp_wasm_builtin_import
#define MICROPY_ENABLE_GC (0)
#define MICROPY_ENABLE_SCHEDULER (0)

#include "extmod/metal/mpconfig_firmware.h"

typedef long mp_off_t;

#ifndef alloca
#define alloca __builtin_alloca
#endif

#define MICROPY_HW_BOARD_NAME "metal-firmware"
#define MICROPY_HW_MCU_NAME "x86_64"
#define MP_STATE_PORT MP_STATE_VM
