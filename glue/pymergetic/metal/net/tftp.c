/*
 * pymergetic.metal.net.tftp — µPy face (callee: src/.../net/tftp).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/tftp/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t tftp_get_async(mp_obj_t server_obj, mp_obj_t name_obj, mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;

    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_net_tftp_get_async(
        (uint32_t)mp_obj_get_int(server_obj), mp_obj_str_get_str(name_obj), (uint8_t *)buf.buf,
        (uint32_t)buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_3(tftp_get_async_obj, tftp_get_async);

static mp_obj_t tftp_poll(void)
{
    pm_metal_net_tftp_poll();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(tftp_poll_obj, tftp_poll);

static mp_obj_t tftp_status(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_tftp_status());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tftp_status_obj, tftp_status);

static mp_obj_t tftp_len(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_tftp_len());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tftp_len_obj, tftp_len);

static mp_obj_t tftp_body(void)
{
    const uint8_t *p = pm_metal_net_tftp_body();
    uint32_t n = pm_metal_net_tftp_len();

    if (p == NULL || n == 0u) {
        return mp_const_none;
    }
    return mp_obj_new_bytes(p, n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tftp_body_obj, tftp_body);

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
    { MP_ROM_QSTR(MP_QSTR_get_async), MP_ROM_PTR(&tftp_get_async_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&tftp_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&tftp_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_len), MP_ROM_PTR(&tftp_len_obj) },
    { MP_ROM_QSTR(MP_QSTR_body), MP_ROM_PTR(&tftp_body_obj) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&tftp_get_obj) },
};
static MP_DEFINE_CONST_DICT(tftp_globals, tftp_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_tftp = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&tftp_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_tftp, "pymergetic.metal.net.tftp", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
