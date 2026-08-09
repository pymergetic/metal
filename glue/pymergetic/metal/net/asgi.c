/*
 * pymergetic.metal.net.asgi — µPy face (callee: src/.../net/asgi).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/asgi/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t asgi_init(mp_obj_t port_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_asgi_init((uint16_t)mp_obj_get_int(port_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(asgi_init_obj, asgi_init);

static mp_obj_t asgi_init_tls(mp_obj_t port_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_asgi_init_tls((uint16_t)mp_obj_get_int(port_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(asgi_init_tls_obj, asgi_init_tls);

static mp_obj_t asgi_poll(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_asgi_poll());
}
static MP_DEFINE_CONST_FUN_OBJ_0(asgi_poll_obj, asgi_poll);

static mp_obj_t asgi_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_asgi_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(asgi_ready_obj, asgi_ready);

static const mp_rom_map_elem_t asgi_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_asgi) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&asgi_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_init_tls), MP_ROM_PTR(&asgi_init_tls_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&asgi_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&asgi_ready_obj) },
};
static MP_DEFINE_CONST_DICT(asgi_globals, asgi_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_asgi = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&asgi_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_asgi, "pymergetic.metal.net.asgi", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
