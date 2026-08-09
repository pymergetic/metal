/*
 * pymergetic.metal.net.faces — µPy face (callee: src/.../net/faces).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/faces/__init__.h>

#include <string.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t faces_mark(mp_obj_t bit_obj)
{
    pm_metal_net_face_mark((uint32_t)mp_obj_get_int(bit_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(faces_mark_obj, faces_mark);

static mp_obj_t faces_bits(void)
{
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_net_face_bits());
}
static MP_DEFINE_CONST_FUN_OBJ_0(faces_bits_obj, faces_bits);

static mp_obj_t faces_format(mp_obj_t cap_obj)
{
    uint32_t cap = (uint32_t)mp_obj_get_int(cap_obj);
    vstr_t vstr;

    if (cap == 0u) {
        mp_raise_ValueError(MP_ERROR_TEXT("faces format"));
    }
    vstr_init_len(&vstr, (size_t)cap);
    pm_metal_net_face_format(vstr.buf, cap);
    {
        mp_obj_t out = mp_obj_new_str(vstr.buf, strlen(vstr.buf));
        vstr_clear(&vstr);
        return out;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_1(faces_format_obj, faces_format);

static const mp_rom_map_elem_t faces_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_faces) },
    { MP_ROM_QSTR(MP_QSTR_mark), MP_ROM_PTR(&faces_mark_obj) },
    { MP_ROM_QSTR(MP_QSTR_bits), MP_ROM_PTR(&faces_bits_obj) },
    { MP_ROM_QSTR(MP_QSTR_format), MP_ROM_PTR(&faces_format_obj) },
};
static MP_DEFINE_CONST_DICT(faces_globals, faces_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_faces = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&faces_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_faces, "pymergetic.metal.net.faces", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
