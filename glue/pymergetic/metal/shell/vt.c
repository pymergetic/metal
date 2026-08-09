/*
 * pymergetic.metal.shell.vt — µPy face (callee: src/.../shell/vt).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/shell/vt/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t vt_init(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_vt_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(vt_init_obj, vt_init);

static mp_obj_t vt_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_vt_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(vt_ready_obj, vt_ready);

static mp_obj_t vt_switch(mp_obj_t index_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_vt_switch((int32_t)mp_obj_get_int(index_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(vt_switch_obj, vt_switch);

static mp_obj_t vt_active(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_vt_active());
}
static MP_DEFINE_CONST_FUN_OBJ_0(vt_active_obj, vt_active);

static mp_obj_t vt_write(mp_obj_t s_obj)
{
    mp_buffer_info_t buf;
    if (mp_obj_is_str(s_obj)) {
        size_t n;
        const char *p = mp_obj_str_get_data(s_obj, &n);
        pm_metal_vt_write(p, n);
    } else {
        mp_get_buffer_raise(s_obj, &buf, MP_BUFFER_READ);
        pm_metal_vt_write((const char *)buf.buf, buf.len);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(vt_write_obj, vt_write);

static mp_obj_t vt_puts(mp_obj_t s_obj)
{
    pm_metal_vt_puts(mp_obj_str_get_str(s_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(vt_puts_obj, vt_puts);

static mp_obj_t vt_render(mp_obj_t index_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_vt_render((int32_t)mp_obj_get_int(index_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(vt_render_obj, vt_render);

/* bind_surface needs C DrawSurface* — expose as no-op success when unbound clear. */
static mp_obj_t vt_bind_surface(mp_obj_t index_obj, mp_obj_t surf_obj)
{
    (void)index_obj;
    if (surf_obj != mp_const_none) {
        mp_raise_ValueError(MP_ERROR_TEXT("vt bind_surface needs C surface"));
    }
    pm_metal_vt_bind_surface((int32_t)mp_obj_get_int(index_obj), NULL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(vt_bind_surface_obj, vt_bind_surface);

static const mp_rom_map_elem_t vt_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_shell_dot_vt) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&vt_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&vt_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_switch), MP_ROM_PTR(&vt_switch_obj) },
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&vt_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&vt_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_puts), MP_ROM_PTR(&vt_puts_obj) },
    { MP_ROM_QSTR(MP_QSTR_render), MP_ROM_PTR(&vt_render_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind_surface), MP_ROM_PTR(&vt_bind_surface_obj) },
};
static MP_DEFINE_CONST_DICT(vt_globals, vt_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_shell_vt = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&vt_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_shell_vt, "pymergetic.metal.shell.vt", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
