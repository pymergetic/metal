/*
 * Extra smoke tests registered with reg (not seat rows): framebuf + network/socket.
 * C callees only — no embedded Python source.
 */
#include "py/mpconfig.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"

#include <pymergetic/metal/reg/seats.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef METAL_BOARD_UEFI
#define METAL_BOARD_UEFI 0
#endif

extern void uart_puts(const char *s);

int32_t pm_metal_smoke_framebuf_test(void)
{
#if !MICROPY_PY_FRAMEBUF
    return 0;
#elif METAL_BOARD_UEFI
    uart_puts("framebuf skip\n");
    return 0;
#else
    {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_obj_t mod = mp_module_get_builtin(MP_QSTR_framebuf, false);
            if (mod == MP_OBJ_NULL) {
                mod = mp_import_name(MP_QSTR_framebuf, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
            }
            nlr_pop();
            if (mod == MP_OBJ_NULL) {
                return -1;
            }
            uart_puts("framebuf ok\n");
            return 0;
        }
        return -1;
    }
#endif
}

#if MICROPY_PY_NETWORK
static int32_t network_test_body(void)
{
    mp_obj_t network;
    mp_obj_t lan_type;
    mp_obj_t lan;
    mp_obj_t dest[4];
    mp_obj_t active;
    mp_obj_t connected;
    mp_obj_t cfg;
    mp_obj_t ip0;
    mp_obj_t sockmod;
    mp_obj_t resolved;
    size_t iplen;
    const char *ips;
    int attempt;

    network = mp_module_get_builtin(MP_QSTR_network, false);
    if (network == MP_OBJ_NULL) {
        network = mp_import_name(MP_QSTR_network, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    }
    if (network == MP_OBJ_NULL) {
        uart_puts("net fail import\n");
        return -1;
    }

    lan_type = mp_load_attr(network, MP_QSTR_LAN);
    lan = mp_call_function_0(lan_type);

    mp_load_method(lan, MP_QSTR_active, dest);
    active = mp_call_method_n_kw(0, 0, dest);
    if (!mp_obj_is_true(active)) {
        uart_puts("net fail active\n");
        return -1;
    }

    mp_load_method(lan, MP_QSTR_isconnected, dest);
    connected = mp_call_method_n_kw(0, 0, dest);
    if (!mp_obj_is_true(connected)) {
        uart_puts("net fail isconnected\n");
        return -1;
    }

    mp_load_method(lan, MP_QSTR_ifconfig, dest);
    cfg = mp_call_method_n_kw(0, 0, dest);
    ip0 = mp_obj_subscr(cfg, MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_SENTINEL);
    ips = mp_obj_str_get_data(ip0, &iplen);
    if (ips == NULL || (iplen >= 7 && memcmp(ips, "0.0.0.0", 7) == 0)) {
        uart_puts("net fail ifconfig\n");
        return -1;
    }
    uart_puts("network ok\n");

    {
        mp_load_method(lan, MP_QSTR_resolve, dest);
        dest[2] = mp_obj_new_str("10.0.2.2", 8);
        resolved = mp_call_method_n_kw(1, 0, dest);
        ips = mp_obj_str_get_data(resolved, &iplen);
        if (ips == NULL || iplen < 7) {
            uart_puts("net fail resolve ip\n");
            return -1;
        }
    }

    resolved = mp_const_none;
    for (attempt = 0; attempt < 3; attempt++) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_load_method(lan, MP_QSTR_resolve, dest);
            dest[2] = mp_obj_new_str("example.com", 11);
            resolved = mp_call_method_n_kw(1, 0, dest);
            nlr_pop();
        } else {
            resolved = mp_const_none;
            continue;
        }
        if (resolved != mp_const_none) {
            ips = mp_obj_str_get_data(resolved, &iplen);
            if (ips != NULL && iplen > 0 && !(iplen >= 7 && memcmp(ips, "0.0.0.0", 7) == 0)) {
                break;
            }
        }
        resolved = mp_const_none;
    }
    if (resolved == mp_const_none) {
        uart_puts("net fail dns\n");
        return -1;
    }
    uart_puts("dns py ok\n");

    sockmod = mp_module_get_builtin(MP_QSTR_socket, false);
    if (sockmod == MP_OBJ_NULL) {
        sockmod = mp_import_name(MP_QSTR_socket, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    }
    if (sockmod == MP_OBJ_NULL) {
        uart_puts("net fail socket import\n");
        return -1;
    }
    uart_puts("socket import ok\n");
    {
        mp_obj_t sock_type = mp_load_attr(sockmod, MP_QSTR_socket);
        mp_obj_t sock = mp_call_function_0(sock_type);
        mp_obj_t gai;
        mp_obj_t first;
        mp_obj_t last;
        mp_obj_t port_obj;
        mp_obj_t args[2];
        mp_int_t port;

        uart_puts("socket new ok\n");
        args[0] = mp_obj_new_str("10.0.2.2", 8);
        args[1] = mp_obj_new_int(80);
        mp_load_method(sockmod, MP_QSTR_getaddrinfo, dest);
        dest[2] = args[0];
        dest[3] = args[1];
        gai = mp_call_method_n_kw(2, 0, dest);
        if (mp_obj_get_int(mp_obj_len(gai)) == 0) {
            uart_puts("net fail gai empty\n");
            return -1;
        }
        first = mp_obj_subscr(gai, MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_SENTINEL);
        last = mp_obj_subscr(first, MP_OBJ_NEW_SMALL_INT(-1), MP_OBJ_SENTINEL);
        port_obj = mp_obj_subscr(last, MP_OBJ_NEW_SMALL_INT(1), MP_OBJ_SENTINEL);
        port = mp_obj_get_int(port_obj);
        if (port != 80) {
            uart_puts("net fail gai port\n");
            return -1;
        }
        uart_puts("socket gai ok\n");
        mp_load_method(sock, MP_QSTR_close, dest);
        (void)mp_call_method_n_kw(0, 0, dest);
        (void)sock;
    }
    uart_puts("socket ok\n");
    return 0;
}
#endif

int32_t pm_metal_smoke_network_test(void)
{
#if !MICROPY_PY_NETWORK
    return 0;
#else
    nlr_buf_t nlr;
    int32_t rc;
    if (nlr_push(&nlr) == 0) {
        rc = network_test_body();
        nlr_pop();
        return rc;
    }
    uart_puts("net fail exc\n");
    return -1;
#endif
}

PM_METAL_REG_SEAT_TEST_ONLY(g_pm_seat_smoke_framebuf, "framebuf", pm_metal_smoke_framebuf_test);
#if MICROPY_PY_NETWORK
PM_METAL_REG_SEAT_TEST_ONLY(g_pm_seat_smoke_network, "network", pm_metal_smoke_network_test);
#endif
