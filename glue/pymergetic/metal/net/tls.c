/*
 * pymergetic.metal.net.tls — µPy face (callee: src/.../net/tls).
 * Firmware seats only. set_ops/ops stay C-only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/tls/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t tls_mbedtls_register(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_tls_mbedtls_register());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tls_mbedtls_register_obj, tls_mbedtls_register);

static mp_obj_t tls_init(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_tls_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tls_init_obj, tls_init);

static mp_obj_t tls_fini(void)
{
    pm_metal_net_tls_fini();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(tls_fini_obj, tls_fini);

static mp_obj_t tls_client_open(mp_obj_t sock_obj, mp_obj_t host_obj, mp_obj_t flags_obj)
{
    const char *host = NULL;
    if (host_obj != mp_const_none) {
        host = mp_obj_str_get_str(host_obj);
    }
    return mp_obj_new_int_from_uint(pm_metal_net_tls_client_open(
        (pm_metal_net_ip_sock_h)mp_obj_get_int(sock_obj), host, (uint32_t)mp_obj_get_int(flags_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(tls_client_open_obj, tls_client_open);

static mp_obj_t tls_server_open(mp_obj_t sock_obj)
{
    return mp_obj_new_int_from_uint(
        pm_metal_net_tls_server_open((pm_metal_net_ip_sock_h)mp_obj_get_int(sock_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tls_server_open_obj, tls_server_open);

static mp_obj_t tls_handshake(mp_obj_t th_obj)
{
    return mp_obj_new_int_from_uint(
        pm_metal_net_tls_handshake((pm_metal_net_tls_h)mp_obj_get_int(th_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tls_handshake_obj, tls_handshake);

static mp_obj_t tls_try_read(mp_obj_t th_obj, mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_net_tls_try_read(
        (pm_metal_net_tls_h)mp_obj_get_int(th_obj), buf.buf, (uint32_t)buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(tls_try_read_obj, tls_try_read);

static mp_obj_t tls_write(mp_obj_t th_obj, mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_READ);
    return mp_obj_new_int_from_uint(pm_metal_net_tls_write(
        (pm_metal_net_tls_h)mp_obj_get_int(th_obj), buf.buf, (uint32_t)buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(tls_write_obj, tls_write);

static mp_obj_t tls_close(mp_obj_t th_obj)
{
    pm_metal_net_tls_close((pm_metal_net_tls_h)mp_obj_get_int(th_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(tls_close_obj, tls_close);

static mp_obj_t tls_load_ca_pem(mp_obj_t pem_obj)
{
    mp_buffer_info_t pem;
    mp_get_buffer_raise(pem_obj, &pem, MP_BUFFER_READ);
    return MP_OBJ_NEW_SMALL_INT(
        pm_metal_net_tls_load_ca_pem((const uint8_t *)pem.buf, (uint32_t)pem.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tls_load_ca_pem_obj, tls_load_ca_pem);

static mp_obj_t tls_load_ca_file(mp_obj_t path_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_tls_load_ca_file(mp_obj_str_get_str(path_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tls_load_ca_file_obj, tls_load_ca_file);

static mp_obj_t tls_set_server_cert_pem(mp_obj_t pem_obj)
{
    mp_buffer_info_t pem;
    mp_get_buffer_raise(pem_obj, &pem, MP_BUFFER_READ);
    return MP_OBJ_NEW_SMALL_INT(
        pm_metal_net_tls_set_server_cert_pem((const uint8_t *)pem.buf, (uint32_t)pem.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tls_set_server_cert_pem_obj, tls_set_server_cert_pem);

static mp_obj_t tls_set_server_key_pem(mp_obj_t pem_obj)
{
    mp_buffer_info_t pem;
    mp_get_buffer_raise(pem_obj, &pem, MP_BUFFER_READ);
    return MP_OBJ_NEW_SMALL_INT(
        pm_metal_net_tls_set_server_key_pem((const uint8_t *)pem.buf, (uint32_t)pem.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tls_set_server_key_pem_obj, tls_set_server_key_pem);

static mp_obj_t tls_set_server_chain_pem(mp_obj_t pem_obj)
{
    mp_buffer_info_t pem;
    mp_get_buffer_raise(pem_obj, &pem, MP_BUFFER_READ);
    return MP_OBJ_NEW_SMALL_INT(
        pm_metal_net_tls_set_server_chain_pem((const uint8_t *)pem.buf, (uint32_t)pem.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tls_set_server_chain_pem_obj, tls_set_server_chain_pem);

static mp_obj_t tls_load_smoke_server(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_tls_load_smoke_server());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tls_load_smoke_server_obj, tls_load_smoke_server);

static mp_obj_t tls_poll(void)
{
    pm_metal_net_tls_poll();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(tls_poll_obj, tls_poll);

/* VERIFY_* constants + INVALID handle → 18 with the 15 callables above. */
static const mp_rom_map_elem_t tls_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_tls) },
    { MP_ROM_QSTR(MP_QSTR_VERIFY_REQUIRED), MP_ROM_INT(PM_METAL_NET_TLS_VERIFY_REQUIRED) },
    { MP_ROM_QSTR(MP_QSTR_VERIFY_NONE), MP_ROM_INT(PM_METAL_NET_TLS_VERIFY_NONE) },
    { MP_ROM_QSTR(MP_QSTR_INVALID), MP_ROM_INT(PM_METAL_NET_TLS_INVALID) },
    { MP_ROM_QSTR(MP_QSTR_mbedtls_register), MP_ROM_PTR(&tls_mbedtls_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&tls_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_fini), MP_ROM_PTR(&tls_fini_obj) },
    { MP_ROM_QSTR(MP_QSTR_client_open), MP_ROM_PTR(&tls_client_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_server_open), MP_ROM_PTR(&tls_server_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_handshake), MP_ROM_PTR(&tls_handshake_obj) },
    { MP_ROM_QSTR(MP_QSTR_try_read), MP_ROM_PTR(&tls_try_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&tls_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&tls_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_ca_pem), MP_ROM_PTR(&tls_load_ca_pem_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_ca_file), MP_ROM_PTR(&tls_load_ca_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_server_cert_pem), MP_ROM_PTR(&tls_set_server_cert_pem_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_server_key_pem), MP_ROM_PTR(&tls_set_server_key_pem_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_server_chain_pem), MP_ROM_PTR(&tls_set_server_chain_pem_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_smoke_server), MP_ROM_PTR(&tls_load_smoke_server_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&tls_poll_obj) },
};
static MP_DEFINE_CONST_DICT(tls_globals, tls_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_tls = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&tls_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_tls, "pymergetic.metal.net.tls", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
