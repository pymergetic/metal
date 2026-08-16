/* Parent package so `import pymergetic.metal as m` LOAD_ATTRs `metal`.
 * Unix keeps the filesystem `pymergetic` package; compile this TU only for
 * firmware µPy and the emcc browser cell. */
#include "extmod/metal/modmetal.h"

#include "py/obj.h"
#include "ports/micropython/importhook.h"
#if defined(__EMSCRIPTEN__) || defined(PM_METAL_FIRMWARE)
#include "ports/micropython/modcdn.h"
#endif

#if defined(__EMSCRIPTEN__) || defined(PM_METAL_FIRMWARE)
static mp_obj_t pymergetic___init__(void) {
    mp_wasm_ensure_inited();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pymergetic___init___obj, pymergetic___init__);
#endif

static const mp_rom_map_elem_t pymergetic_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic) },
#if defined(__EMSCRIPTEN__) || defined(PM_METAL_FIRMWARE)
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&pymergetic___init___obj) },
#endif
    { MP_ROM_QSTR(MP_QSTR_metal), MP_ROM_PTR(&mp_module_pymergetic_metal) },
#if defined(__EMSCRIPTEN__) || defined(PM_METAL_FIRMWARE)
    { MP_ROM_QSTR(MP_QSTR_wasmmod), MP_ROM_PTR(&mp_module_pymergetic_wasmmod) },
#endif
};
static MP_DEFINE_CONST_DICT(pymergetic_globals, pymergetic_globals_table);

const mp_obj_module_t mp_module_pymergetic = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&pymergetic_globals,
};

#if MICROPY_MODULE_ATTR_DELEGATION
MP_REGISTER_MODULE_DELEGATION(mp_module_pymergetic, mp_wasm_pymergetic_attr);
#endif
MP_REGISTER_MODULE(MP_QSTR_pymergetic, mp_module_pymergetic);
