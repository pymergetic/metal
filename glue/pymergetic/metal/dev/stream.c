/*
 * pymergetic.metal.dev.stream — µPy face (callee: src/.../dev/stream).
 * Core face (API=4): pipe, write, try_read, close.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/stream/__init__.h>

static mp_obj_t stream_pipe(void)
{
    pm_metal_stream_h r = 0, w = 0;
    mp_obj_t items[2];
    int32_t rc = pm_metal_stream_pipe(&r, &w);
    if (rc != 0) {
        return MP_OBJ_NEW_SMALL_INT(rc);
    }
    items[0] = mp_obj_new_int_from_uint(r);
    items[1] = mp_obj_new_int_from_uint(w);
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(stream_pipe_obj, stream_pipe);

static mp_obj_t stream_write(mp_obj_t h_obj, mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_READ);
    return mp_obj_new_int_from_uint(pm_metal_stream_write((pm_metal_stream_h)mp_obj_get_int(h_obj),
                                                          buf.buf, (uint32_t)buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(stream_write_obj, stream_write);

static mp_obj_t stream_try_read(mp_obj_t h_obj, mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_stream_try_read(
        (pm_metal_stream_h)mp_obj_get_int(h_obj), buf.buf, (uint32_t)buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(stream_try_read_obj, stream_try_read);

static mp_obj_t stream_close(mp_obj_t h_obj)
{
    pm_metal_stream_close((pm_metal_stream_h)mp_obj_get_int(h_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(stream_close_obj, stream_close);

static const mp_rom_map_elem_t stream_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_stream) },
    { MP_ROM_QSTR(MP_QSTR_pipe), MP_ROM_PTR(&stream_pipe_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_try_read), MP_ROM_PTR(&stream_try_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&stream_close_obj) },
};
static MP_DEFINE_CONST_DICT(stream_globals, stream_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_stream = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&stream_globals,
};
