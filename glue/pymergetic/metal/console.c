/*
 * pymergetic.metal.console — µPy face.
 * Firmware seats only. Sink callbacks are C fn pointers — Py face clears/ignores them.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/console.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t console_init(mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return MP_OBJ_NEW_SMALL_INT(pm_metal_console_init((uint8_t *)buf.buf, buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(console_init_obj, console_init);

static mp_obj_t console_create(mp_obj_t id_obj, mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return MP_OBJ_NEW_SMALL_INT(
        pm_metal_console_create((int32_t)mp_obj_get_int(id_obj), (uint8_t *)buf.buf, buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(console_create_obj, console_create);

static mp_obj_t console_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_console_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(console_ready_obj, console_ready);

static mp_obj_t console_ready_id(mp_obj_t id_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_console_ready_id((int32_t)mp_obj_get_int(id_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(console_ready_id_obj, console_ready_id);

static mp_obj_t console_write(mp_obj_t data_obj)
{
    mp_buffer_info_t data;
    mp_get_buffer_raise(data_obj, &data, MP_BUFFER_READ);
    return mp_obj_new_int((mp_int_t)pm_metal_console_write((const uint8_t *)data.buf, data.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(console_write_obj, console_write);

static mp_obj_t console_write_id(mp_obj_t id_obj, mp_obj_t data_obj)
{
    mp_buffer_info_t data;
    mp_get_buffer_raise(data_obj, &data, MP_BUFFER_READ);
    return mp_obj_new_int((mp_int_t)pm_metal_console_write_id((int32_t)mp_obj_get_int(id_obj),
                                                              (const uint8_t *)data.buf, data.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(console_write_id_obj, console_write_id);

static mp_obj_t console_viewport_attach(mp_obj_t console_id_obj)
{
    /* Sink trampoline not exposed on this face — attach with NULL sink. */
    return MP_OBJ_NEW_SMALL_INT(
        pm_metal_console_viewport_attach((int32_t)mp_obj_get_int(console_id_obj), NULL, NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_1(console_viewport_attach_obj, console_viewport_attach);

static mp_obj_t console_attach(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_console_attach(NULL, NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_0(console_attach_obj, console_attach);

static mp_obj_t console_viewport_rebind(mp_obj_t vp_obj, mp_obj_t console_id_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_console_viewport_rebind(
        (pm_metal_console_vp_id)mp_obj_get_int(vp_obj), (int32_t)mp_obj_get_int(console_id_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(console_viewport_rebind_obj, console_viewport_rebind);

static mp_obj_t console_viewport_detach(mp_obj_t vp_obj)
{
    pm_metal_console_viewport_detach((pm_metal_console_vp_id)mp_obj_get_int(vp_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(console_viewport_detach_obj, console_viewport_detach);

static mp_obj_t console_detach(void)
{
    pm_metal_console_detach();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(console_detach_obj, console_detach);

static mp_obj_t console_set_sink(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_console_set_sink(NULL, NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_0(console_set_sink_obj, console_set_sink);

static mp_obj_t console_seq(void)
{
    return mp_obj_new_int_from_ull(pm_metal_console_seq());
}
static MP_DEFINE_CONST_FUN_OBJ_0(console_seq_obj, console_seq);

static mp_obj_t console_len(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_console_len());
}
static MP_DEFINE_CONST_FUN_OBJ_0(console_len_obj, console_len);

static mp_obj_t console_copy_tail(mp_obj_t out_obj)
{
    mp_buffer_info_t out;
    mp_get_buffer_raise(out_obj, &out, MP_BUFFER_WRITE);
    return mp_obj_new_int((mp_int_t)pm_metal_console_copy_tail((uint8_t *)out.buf, out.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(console_copy_tail_obj, console_copy_tail);

static const mp_rom_map_elem_t console_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_console) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&console_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&console_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&console_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready_id), MP_ROM_PTR(&console_ready_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&console_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_id), MP_ROM_PTR(&console_write_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_viewport_attach), MP_ROM_PTR(&console_viewport_attach_obj) },
    { MP_ROM_QSTR(MP_QSTR_attach), MP_ROM_PTR(&console_attach_obj) },
    { MP_ROM_QSTR(MP_QSTR_viewport_rebind), MP_ROM_PTR(&console_viewport_rebind_obj) },
    { MP_ROM_QSTR(MP_QSTR_viewport_detach), MP_ROM_PTR(&console_viewport_detach_obj) },
    { MP_ROM_QSTR(MP_QSTR_detach), MP_ROM_PTR(&console_detach_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_sink), MP_ROM_PTR(&console_set_sink_obj) },
    { MP_ROM_QSTR(MP_QSTR_seq), MP_ROM_PTR(&console_seq_obj) },
    { MP_ROM_QSTR(MP_QSTR_len), MP_ROM_PTR(&console_len_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy_tail), MP_ROM_PTR(&console_copy_tail_obj) },
};
static MP_DEFINE_CONST_DICT(console_globals, console_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_console = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&console_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_console, "pymergetic.metal.console", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
