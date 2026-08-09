/*
 * network.LAN — status face over Metal lwIP eth0 (sockets are modlwip).
 */
#include <stdio.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "extmod/modnetwork.h"

#if MICROPY_PY_NETWORK && MICROPY_PY_LWIP

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "pymergetic/metal/net/dns/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/cfg.h"
#include "pymergetic/metal/net/nic/__init__.h"

typedef struct {
    mp_obj_base_t base;
    struct netif *netif;
} network_metal_nic_obj_t;

static network_metal_nic_obj_t network_metal_nic_obj;

static mp_obj_t network_metal_nic_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw,
                                           const mp_obj_t *args)
{
    (void)type;
    (void)n_args;
    (void)n_kw;
    (void)args;
    network_metal_nic_obj.base.type = &mp_network_metal_nic_type;
    if (network_metal_nic_obj.netif == NULL) {
        network_metal_nic_obj.netif = netif_default;
    }
    return MP_OBJ_FROM_PTR(&network_metal_nic_obj);
}

static mp_obj_t network_metal_nic_active(size_t n_args, const mp_obj_t *args)
{
    network_metal_nic_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args == 1) {
        return mp_obj_new_bool(self->netif != NULL && netif_is_up(self->netif));
    }
    if (mp_obj_is_true(args[1])) {
        if (self->netif != NULL) {
            netif_set_up(self->netif);
        }
    } else if (self->netif != NULL) {
        netif_set_down(self->netif);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(network_metal_nic_active_obj, 1, 2, network_metal_nic_active);

static mp_obj_t network_metal_nic_isconnected(mp_obj_t self_in)
{
    network_metal_nic_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(self->netif != NULL && netif_is_link_up(self->netif) &&
                           !ip4_addr_isany_val(*netif_ip4_addr(self->netif)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(network_metal_nic_isconnected_obj, network_metal_nic_isconnected);

static mp_obj_t network_metal_nic_ifconfig(size_t n_args, const mp_obj_t *args)
{
    network_metal_nic_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->netif == NULL) {
        self->netif = netif_default;
    }
    if (self->netif == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no netif"));
    }
    return mod_network_nic_ifconfig(self->netif, n_args - 1, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(network_metal_nic_ifconfig_obj, 1, 2,
                                           network_metal_nic_ifconfig);

static mp_obj_t network_metal_nic_status(mp_obj_t self_in)
{
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(network_metal_nic_status_obj, network_metal_nic_status);

static mp_obj_t network_metal_nic_resolve(mp_obj_t self_in, mp_obj_t host_in)
{
    const char *host = mp_obj_str_get_str(host_in);
    uint32_t ip = 0;
    char buf[16];
    (void)self_in;
    if (pm_metal_net_ip_resolve_ip4(host, &ip) != 0) {
        if (pm_metal_net_dns_resolve(host, &ip) != 0 || ip == 0u) {
            mp_raise_OSError(MP_EIO);
        }
    }
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (unsigned)((ip >> 24) & 0xffu),
             (unsigned)((ip >> 16) & 0xffu), (unsigned)((ip >> 8) & 0xffu),
             (unsigned)(ip & 0xffu));
    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_2(network_metal_nic_resolve_obj, network_metal_nic_resolve);

static const mp_rom_map_elem_t network_metal_nic_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&network_metal_nic_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_isconnected), MP_ROM_PTR(&network_metal_nic_isconnected_obj) },
    { MP_ROM_QSTR(MP_QSTR_ifconfig), MP_ROM_PTR(&network_metal_nic_ifconfig_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&network_metal_nic_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_resolve), MP_ROM_PTR(&network_metal_nic_resolve_obj) },
};
static MP_DEFINE_CONST_DICT(network_metal_nic_locals_dict, network_metal_nic_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_network_metal_nic_type,
    MP_QSTR_LAN,
    MP_TYPE_FLAG_NONE,
    make_new, network_metal_nic_make_new,
    locals_dict, &network_metal_nic_locals_dict);

int32_t pm_metal_net_nic_attach_upy(void)
{
    network_metal_nic_obj.base.type = &mp_network_metal_nic_type;
    network_metal_nic_obj.netif = netif_default;
    mod_network_register_nic(MP_OBJ_FROM_PTR(&network_metal_nic_obj));
    return 0;
}

#endif /* MICROPY_PY_NETWORK && MICROPY_PY_LWIP */
