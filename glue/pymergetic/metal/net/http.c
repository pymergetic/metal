/*
 * pymergetic.metal.net.http — µPy face (callee: src/.../net/http).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/http/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t http_init(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_http_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_init_obj, http_init);

static mp_obj_t http_init_tls(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_http_init_tls());
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_init_tls_obj, http_init_tls);

static mp_obj_t http_shutdown(void)
{
    pm_metal_net_http_shutdown();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_shutdown_obj, http_shutdown);

static mp_obj_t http_poll(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_http_poll());
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_poll_obj, http_poll);

static mp_obj_t http_served(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_http_served());
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_served_obj, http_served);

static mp_obj_t http_get(mp_obj_t url_obj)
{
    return mp_obj_new_int_from_uint(pm_metal_net_http_get(mp_obj_str_get_str(url_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(http_get_obj, http_get);

static mp_obj_t http_status(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_http_status());
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_status_obj, http_status);

static mp_obj_t http_body_len(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_http_body_len());
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_body_len_obj, http_body_len);

static mp_obj_t http_body(void)
{
    uint32_t n = pm_metal_net_http_body_len();
    const uint8_t *p = pm_metal_net_http_body();
    if (p == NULL || n == 0u) {
        return mp_const_empty_bytes;
    }
    return mp_obj_new_bytes(p, n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_body_obj, http_body);

static mp_obj_t http_set_tls_verify_none(mp_obj_t on_obj)
{
    pm_metal_net_http_set_tls_verify_none((int32_t)mp_obj_get_int(on_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(http_set_tls_verify_none_obj, http_set_tls_verify_none);

static mp_obj_t http_client_poll(void)
{
    pm_metal_net_http_client_poll();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_client_poll_obj, http_client_poll);

static const mp_rom_map_elem_t http_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_http) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&http_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_init_tls), MP_ROM_PTR(&http_init_tls_obj) },
    { MP_ROM_QSTR(MP_QSTR_shutdown), MP_ROM_PTR(&http_shutdown_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&http_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_served), MP_ROM_PTR(&http_served_obj) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&http_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&http_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_body_len), MP_ROM_PTR(&http_body_len_obj) },
    { MP_ROM_QSTR(MP_QSTR_body), MP_ROM_PTR(&http_body_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_tls_verify_none), MP_ROM_PTR(&http_set_tls_verify_none_obj) },
    { MP_ROM_QSTR(MP_QSTR_client_poll), MP_ROM_PTR(&http_client_poll_obj) },
};
static MP_DEFINE_CONST_DICT(http_globals, http_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_http = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&http_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_http, "pymergetic.metal.net.http", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
