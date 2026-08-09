/*
 * pymergetic.metal.dev.serial — µPy face (callee: src/.../dev/serial).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/serial.h>

static mp_obj_t serial_write(mp_obj_t data_obj)
{
    mp_buffer_info_t data;

    mp_get_buffer_raise(data_obj, &data, MP_BUFFER_READ);
    pm_metal_dev_serial_write((const uint8_t *)data.buf, data.len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(serial_write_obj, serial_write);

static mp_obj_t serial_console_sink(mp_obj_t data_obj)
{
    mp_buffer_info_t data;

    mp_get_buffer_raise(data_obj, &data, MP_BUFFER_READ);
    pm_metal_dev_serial_console_sink((const uint8_t *)data.buf, data.len, NULL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(serial_console_sink_obj, serial_console_sink);

static const mp_rom_map_elem_t serial_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_serial) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&serial_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_console_sink), MP_ROM_PTR(&serial_console_sink_obj) },
};
static MP_DEFINE_CONST_DICT(serial_globals, serial_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_serial = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&serial_globals,
};
