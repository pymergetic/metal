/*
 * Firmware µPy seat (BIOS/UEFI/RV1106). GC off, scheduler off.
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
#if defined(PM_METAL_UART_REPL)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_USE_READLINE (1)
#define MICROPY_USE_READLINE_HISTORY (1)
#else
#define MICROPY_HELPER_REPL (0)
#endif
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

void pm_metal_boot_motd(void);
#define MICROPY_PYEXEC_BANNER_HOOK pm_metal_boot_motd()

#ifndef MICROPY_REPL_PS1
#define MICROPY_REPL_PS1 "\033[36m>>>\033[0m "
#endif
#ifndef MICROPY_REPL_PS2
#define MICROPY_REPL_PS2 "\033[2m...\033[0m "
#endif

extern const struct _mp_obj_fun_builtin_var_t mp_metal_builtin_quit_obj;
extern const struct _mp_obj_fun_builtin_fixed_t mp_metal_builtin_reboot_obj;
extern const struct _mp_obj_fun_builtin_fixed_t mp_metal_builtin_shutdown_obj;
extern const struct _mp_obj_fun_builtin_fixed_t mp_metal_builtin_process_obj;
extern const struct _mp_obj_fun_builtin_var_t mp_metal_builtin_packages_obj;
extern const struct _mp_obj_fun_builtin_fixed_t mp_metal_builtin_help_obj;

#define MICROPY_PORT_BUILTINS \
    { MP_ROM_QSTR(MP_QSTR_quit), MP_ROM_PTR(&mp_metal_builtin_quit_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_exit), MP_ROM_PTR(&mp_metal_builtin_quit_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_reboot), MP_ROM_PTR(&mp_metal_builtin_reboot_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_shutdown), MP_ROM_PTR(&mp_metal_builtin_shutdown_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_process), MP_ROM_PTR(&mp_metal_builtin_process_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_packages), MP_ROM_PTR(&mp_metal_builtin_packages_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&mp_metal_builtin_help_obj) },

typedef long mp_off_t;

#ifndef alloca
#define alloca __builtin_alloca
#endif

#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME "metal-firmware"
#endif
#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME "x86_64"
#endif
#define MP_STATE_PORT MP_STATE_VM
