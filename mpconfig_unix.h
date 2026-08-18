/*
 * Unix MICROPY_PY_METAL=1. Keep µPy GC. Packet / io.fetch bytes on util.mem.
 * Firmware images use ports/freestanding/mpconfig_freestanding.h (GC off).
 */
#ifndef PYMERGETIC_METAL_MPCONFIG_UNIX_H
#define PYMERGETIC_METAL_MPCONFIG_UNIX_H

#include <stddef.h>

void *pm_metal_wasm_malloc(size_t n);
void pm_metal_wasm_free(void *p);
void *pm_metal_wasm_realloc(void *p, size_t n);

#ifndef MICROPY_WASM_MALLOC
#define MICROPY_WASM_MALLOC(sz) pm_metal_wasm_malloc(sz)
#define MICROPY_WASM_FREE(p) pm_metal_wasm_free(p)
#define MICROPY_WASM_REALLOC(p, sz) pm_metal_wasm_realloc((p), (sz))
#endif

/* Owns port init on this seat regardless of -include order with
 * mpconfig_wasm.h; pm_metal_upy_port_init() calls mp_wasm_port_init(). */
void pm_metal_upy_port_init(void);
#undef MICROPY_PORT_INIT_FUNC
#define MICROPY_PORT_INIT_FUNC pm_metal_upy_port_init()

/* Same MOTD under the µPy banner on unix and emcc (firmware sets this in
 * port/mpconfigport.h). */
void pm_metal_boot_motd(void);
#undef MICROPY_PYEXEC_BANNER_HOOK
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

/* Stock help() wins first-match in the ROM map. Metal owns help(). */
#undef MICROPY_PY_BUILTINS_HELP
#define MICROPY_PY_BUILTINS_HELP (0)
#undef MICROPY_PY_BUILTINS_HELP_MODULES
#define MICROPY_PY_BUILTINS_HELP_MODULES (0)

#undef MICROPY_PORT_BUILTINS
#define MICROPY_PORT_BUILTINS \
    { MP_ROM_QSTR(MP_QSTR_quit), MP_ROM_PTR(&mp_metal_builtin_quit_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_exit), MP_ROM_PTR(&mp_metal_builtin_quit_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_reboot), MP_ROM_PTR(&mp_metal_builtin_reboot_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_shutdown), MP_ROM_PTR(&mp_metal_builtin_shutdown_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_process), MP_ROM_PTR(&mp_metal_builtin_process_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_packages), MP_ROM_PTR(&mp_metal_builtin_packages_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&mp_metal_builtin_help_obj) },

#endif /* PYMERGETIC_METAL_MPCONFIG_UNIX_H */
