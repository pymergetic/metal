/*
 * pymergetic.metal.dev.input.kbd — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/input/kbd.h>

static mp_obj_t kbd_init(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_kbd_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(kbd_init_obj, kbd_init);

static mp_obj_t kbd_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_kbd_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(kbd_ready_obj, kbd_ready);

static mp_obj_t kbd_set_fn_callback(mp_obj_t cb_obj, mp_obj_t user_obj)
{
    (void)cb_obj;
    (void)user_obj;
    /* Thin face: clear callback (Py callables need a host trampoline). */
    pm_metal_kbd_set_fn_callback(NULL, NULL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(kbd_set_fn_callback_obj, kbd_set_fn_callback);

static mp_obj_t kbd_feed_scancode(mp_obj_t sc_obj)
{
    pm_metal_kbd_feed_scancode((uint8_t)mp_obj_get_int(sc_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(kbd_feed_scancode_obj, kbd_feed_scancode);

static mp_obj_t kbd_poll(void)
{
    pm_metal_kbd_poll();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(kbd_poll_obj, kbd_poll);

static const mp_rom_map_elem_t kbd_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_input_dot_kbd) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&kbd_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&kbd_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_fn_callback), MP_ROM_PTR(&kbd_set_fn_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_feed_scancode), MP_ROM_PTR(&kbd_feed_scancode_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&kbd_poll_obj) },
};
static MP_DEFINE_CONST_DICT(kbd_globals, kbd_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_input_kbd = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&kbd_globals,
};
