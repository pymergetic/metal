/*
 * pymergetic.metal.net.tftp — µPy face (callee: src/.../net/tftp).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/tftp/__init__.h>

static mp_obj_t tftp_get(mp_obj_t server_obj, mp_obj_t name_obj, mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    uint32_t len_out = 0;
    int32_t rc;

    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    rc = pm_metal_net_tftp_get((uint32_t)mp_obj_get_int(server_obj), mp_obj_str_get_str(name_obj),
                               (uint8_t *)buf.buf, (uint32_t)buf.len, &len_out);
    if (rc != 0) {
        return MP_OBJ_NEW_SMALL_INT(rc);
    }
    return mp_obj_new_int_from_uint(len_out);
}
static MP_DEFINE_CONST_FUN_OBJ_3(tftp_get_obj, tftp_get);

static const mp_rom_map_elem_t tftp_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_tftp) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&tftp_get_obj) },
};
static MP_DEFINE_CONST_DICT(tftp_globals, tftp_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_tftp = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&tftp_globals,
};
