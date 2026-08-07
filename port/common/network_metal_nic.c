/*
 * Metal AbstractNIC — µPy network.LAN over mini-IP (L2 register + ifconfig).
 * Socket face is stubbed (EOPNOTSUPP) until mini-TCP grows a stream API.
 */
#include "py/mperrno.h"
#include "py/obj.h"
#include "py/runtime.h"

#if MICROPY_PY_NETWORK

#include "extmod/modnetwork.h"
#include "shared/netutils/netutils.h"

#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/upy_nic.h"

#include <stdint.h>

typedef struct _metal_nic_obj_t {
    mp_obj_base_t base;
    bool active;
} metal_nic_obj_t;

const mp_obj_type_t mp_network_metal_nic_type;

static metal_nic_obj_t metal_nic_singleton = {
    .base = { &mp_network_metal_nic_type },
    .active = false,
};

static void ipv4_to_octets(uint32_t be, uint8_t out[4])
{
    out[0] = (uint8_t)(be >> 24);
    out[1] = (uint8_t)(be >> 16);
    out[2] = (uint8_t)(be >> 8);
    out[3] = (uint8_t)be;
}

static int metal_gethostbyname(mp_obj_t nic, const char *name, mp_uint_t len, uint8_t *ip_out)
{
    (void)nic;
    (void)name;
    (void)len;
    (void)ip_out;
    return MP_EOPNOTSUPP;
}

static void metal_deinit(void)
{
    metal_nic_singleton.active = false;
}

static int metal_socket(mod_network_socket_obj_t *socket, int *_errno)
{
    (void)socket;
    *_errno = MP_EOPNOTSUPP;
    return -1;
}

static void metal_close(mod_network_socket_obj_t *socket)
{
    (void)socket;
}

static int metal_bind(mod_network_socket_obj_t *socket, byte *ip, mp_uint_t port, int *_errno)
{
    (void)socket;
    (void)ip;
    (void)port;
    *_errno = MP_EOPNOTSUPP;
    return -1;
}

static int metal_listen(mod_network_socket_obj_t *socket, mp_int_t backlog, int *_errno)
{
    (void)socket;
    (void)backlog;
    *_errno = MP_EOPNOTSUPP;
    return -1;
}

static int metal_accept(mod_network_socket_obj_t *socket, mod_network_socket_obj_t *socket2, byte *ip,
                        mp_uint_t *port, int *_errno)
{
    (void)socket;
    (void)socket2;
    (void)ip;
    (void)port;
    *_errno = MP_EOPNOTSUPP;
    return -1;
}

static int metal_connect(mod_network_socket_obj_t *socket, byte *ip, mp_uint_t port, int *_errno)
{
    (void)socket;
    (void)ip;
    (void)port;
    *_errno = MP_EOPNOTSUPP;
    return -1;
}

static mp_uint_t metal_send(mod_network_socket_obj_t *socket, const byte *buf, mp_uint_t len, int *_errno)
{
    (void)socket;
    (void)buf;
    (void)len;
    *_errno = MP_EOPNOTSUPP;
    return (mp_uint_t)-1;
}

static mp_uint_t metal_recv(mod_network_socket_obj_t *socket, byte *buf, mp_uint_t len, int *_errno)
{
    (void)socket;
    (void)buf;
    (void)len;
    *_errno = MP_EOPNOTSUPP;
    return (mp_uint_t)-1;
}

static mp_uint_t metal_sendto(mod_network_socket_obj_t *socket, const byte *buf, mp_uint_t len, byte *ip,
                              mp_uint_t port, int *_errno)
{
    (void)socket;
    (void)buf;
    (void)len;
    (void)ip;
    (void)port;
    *_errno = MP_EOPNOTSUPP;
    return (mp_uint_t)-1;
}

static mp_uint_t metal_recvfrom(mod_network_socket_obj_t *socket, byte *buf, mp_uint_t len, byte *ip,
                                mp_uint_t *port, int *_errno)
{
    (void)socket;
    (void)buf;
    (void)len;
    (void)ip;
    (void)port;
    *_errno = MP_EOPNOTSUPP;
    return (mp_uint_t)-1;
}

static int metal_setsockopt(mod_network_socket_obj_t *socket, mp_uint_t level, mp_uint_t opt,
                            const void *optval, mp_uint_t optlen, int *_errno)
{
    (void)socket;
    (void)level;
    (void)opt;
    (void)optval;
    (void)optlen;
    *_errno = 0;
    return 0;
}

static int metal_settimeout(mod_network_socket_obj_t *socket, mp_uint_t timeout_ms, int *_errno)
{
    (void)socket;
    (void)timeout_ms;
    *_errno = 0;
    return 0;
}

static int metal_ioctl(mod_network_socket_obj_t *socket, mp_uint_t request, mp_uint_t arg, int *_errno)
{
    (void)socket;
    (void)request;
    (void)arg;
    *_errno = MP_EINVAL;
    return -1;
}

static const mod_network_nic_protocol_t metal_nic_protocol = {
    .gethostbyname = metal_gethostbyname,
    .deinit = metal_deinit,
    .socket = metal_socket,
    .close = metal_close,
    .bind = metal_bind,
    .listen = metal_listen,
    .accept = metal_accept,
    .connect = metal_connect,
    .send = metal_send,
    .recv = metal_recv,
    .sendto = metal_sendto,
    .recvfrom = metal_recvfrom,
    .setsockopt = metal_setsockopt,
    .settimeout = metal_settimeout,
    .ioctl = metal_ioctl,
};

static mp_obj_t metal_nic_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    (void)type;
    (void)n_args;
    (void)n_kw;
    (void)args;
    metal_nic_singleton.active = true;
    mod_network_register_nic(MP_OBJ_FROM_PTR(&metal_nic_singleton));
    return MP_OBJ_FROM_PTR(&metal_nic_singleton);
}

static mp_obj_t metal_nic_active(size_t n_args, const mp_obj_t *args)
{
    metal_nic_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args == 1) {
        return mp_obj_new_bool(self->active);
    }
    self->active = mp_obj_is_true(args[1]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(metal_nic_active_obj, 1, 2, metal_nic_active);

static mp_obj_t metal_nic_isconnected(mp_obj_t self_in)
{
    (void)self_in;
    return mp_obj_new_bool(pm_metal_ip_ready() && pm_metal_ip_addr() != 0u);
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_nic_isconnected_obj, metal_nic_isconnected);

static mp_obj_t metal_nic_ifconfig(size_t n_args, const mp_obj_t *args)
{
    uint8_t ip[4], mask[4], gw[4], dns[4];
    mp_obj_t tuple[4];

    (void)args;
    if (n_args != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("ifconfig set not supported"));
    }
    ipv4_to_octets(pm_metal_ip_addr(), ip);
    ipv4_to_octets(pm_metal_ip_mask(), mask);
    ipv4_to_octets(pm_metal_ip_gw(), gw);
    ipv4_to_octets(pm_metal_ip_dns(), dns);
    tuple[0] = netutils_format_ipv4_addr(ip, NETUTILS_BIG);
    tuple[1] = netutils_format_ipv4_addr(mask, NETUTILS_BIG);
    tuple[2] = netutils_format_ipv4_addr(gw, NETUTILS_BIG);
    tuple[3] = netutils_format_ipv4_addr(dns, NETUTILS_BIG);
    return mp_obj_new_tuple(4, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(metal_nic_ifconfig_obj, 1, 2, metal_nic_ifconfig);

static mp_obj_t metal_nic_status(mp_obj_t self_in)
{
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_upy_nic_ops() != NULL ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_nic_status_obj, metal_nic_status);

static const mp_rom_map_elem_t metal_nic_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&metal_nic_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_isconnected), MP_ROM_PTR(&metal_nic_isconnected_obj) },
    { MP_ROM_QSTR(MP_QSTR_ifconfig), MP_ROM_PTR(&metal_nic_ifconfig_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&metal_nic_status_obj) },
};
static MP_DEFINE_CONST_DICT(metal_nic_locals_dict, metal_nic_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_network_metal_nic_type, MP_QSTR_LAN, MP_TYPE_FLAG_NONE,
    make_new, metal_nic_make_new,
    protocol, &metal_nic_protocol,
    locals_dict, &metal_nic_locals_dict);

int32_t pm_metal_net_upy_nic_attach_upy(void)
{
    if (pm_metal_net_upy_nic_ops() == NULL) {
        return -1;
    }
    metal_nic_singleton.active = true;
    mod_network_register_nic(MP_OBJ_FROM_PTR(&metal_nic_singleton));
    return 0;
}

#endif /* MICROPY_PY_NETWORK */
