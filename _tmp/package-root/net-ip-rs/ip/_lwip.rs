//! Private lwIP C FFI (bodies in `external/lwip`, linked by forge).
//!
//! Layout matches `cfg/lwipopts.h` (`LWIP_IPV4=1`, `LWIP_IPV6=0`,
//! `LWIP_ETHERNET=1`, `LWIP_DHCP=1`, `LWIP_NETIF_LOOPBACK=1`,
//! `LWIP_NETIF_HOSTNAME=1`); size asserts fail the build if opts drift.
use core::ffi::{c_char, c_void};

// ---------------------------------------------------------------------------
// err_t (lwip/err.h)
// ---------------------------------------------------------------------------

pub type err_t = i8;

pub const ERR_OK: err_t = 0;
pub const ERR_MEM: err_t = -1;
pub const ERR_BUF: err_t = -2;
pub const ERR_TIMEOUT: err_t = -3;
pub const ERR_RTE: err_t = -4;
pub const ERR_INPROGRESS: err_t = -5;
pub const ERR_VAL: err_t = -6;
pub const ERR_USE: err_t = -8;
pub const ERR_ABRT: err_t = -13;
pub const ERR_IF: err_t = -12;
pub const ERR_ARG: err_t = -16;

// ---------------------------------------------------------------------------
// Addresses (lwip/ip4_addr.h, lwip/ip_addr.h)
// ---------------------------------------------------------------------------

/// `ip4_addr_t` — IPv4 address in network byte order.
#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub struct ip4_addr_t {
    pub addr: u32,
}

/// `ip_addr_t`. With `LWIP_IPV6=0` lwIP typedefs it straight to `ip4_addr_t`.
pub type ip_addr_t = ip4_addr_t;

const _: () = assert!(core::mem::size_of::<ip_addr_t>() == 4);

pub const IPADDR_TYPE_V4: u8 = 0;
pub const IP_ADDR_ANY: ip_addr_t = ip4_addr_t { addr: 0 };
pub const LWIP_DNS_ADDRTYPE_IPV4: u8 = 0;

#[inline]
pub const fn ip4_addr(a: u8, b: u8, c: u8, d: u8) -> ip4_addr_t {
    ip4_addr_t {
        addr: u32::from_be_bytes([a, b, c, d]),
    }
}

pub unsafe fn ip_addr_eq(a: *const ip_addr_t, b: *const ip_addr_t) -> bool {
    !a.is_null() && !b.is_null() && (*a).addr == (*b).addr
}

pub unsafe fn aton(host: *const c_char) -> Option<ip4_addr_t> {
    let mut out = ip4_addr_t::default();
    if ip4addr_aton(host, &mut out) != 0 {
        Some(out)
    } else {
        None
    }
}

// ---------------------------------------------------------------------------
// pbuf (lwip/pbuf.h)
// ---------------------------------------------------------------------------

#[repr(C)]
pub struct pbuf {
    pub next: *mut pbuf,
    pub payload: *mut c_void,
    pub tot_len: u16,
    pub len: u16,
    pub type_internal: u8,
    pub flags: u8,
    pub r#ref: u8,
    pub if_idx: u8,
}

const _: () = assert!(core::mem::size_of::<pbuf>() == 24);

pub const PBUF_RAW: u32 = 0;
pub const PBUF_IP: u32 = 34;
pub const PBUF_TRANSPORT: u32 = 54;
pub const PBUF_RAM: u32 = 0x0280;
pub const PBUF_POOL: u32 = 386;

// ---------------------------------------------------------------------------
// netif (lwip/netif.h) — Metal lwipopts layout, sizeof 112
// ---------------------------------------------------------------------------

pub const NETIF_FLAG_UP: u8 = 0x01;
pub const NETIF_FLAG_BROADCAST: u8 = 0x02;
pub const NETIF_FLAG_LINK_UP: u8 = 0x04;
pub const NETIF_FLAG_ETHARP: u8 = 0x08;
pub const NETIF_FLAG_ETHERNET: u8 = 0x10;
pub const ETH_HWADDR_LEN: u8 = 6;

pub type netif_input_fn =
    unsafe extern "C" fn(p: *mut pbuf, inp: *mut netif) -> err_t;
pub type netif_output_fn =
    unsafe extern "C" fn(netif: *mut netif, p: *mut pbuf, ipaddr: *const ip4_addr_t) -> err_t;
pub type netif_linkoutput_fn =
    unsafe extern "C" fn(netif: *mut netif, p: *mut pbuf) -> err_t;
pub type netif_init_fn = unsafe extern "C" fn(netif: *mut netif) -> err_t;

#[repr(C)]
pub struct netif {
    pub next: *mut netif,
    pub ip_addr: ip_addr_t,
    pub netmask: ip_addr_t,
    pub gw: ip_addr_t,
    pub input: Option<netif_input_fn>,
    pub output: Option<netif_output_fn>,
    pub linkoutput: Option<netif_linkoutput_fn>,
    pub state: *mut c_void,
    pub client_data: [*mut c_void; 1],
    pub hostname: *const c_char,
    pub mtu: u16,
    pub hwaddr: [u8; 6],
    pub hwaddr_len: u8,
    pub flags: u8,
    pub name: [c_char; 2],
    pub num: u8,
    pub loop_first: *mut pbuf,
    pub loop_last: *mut pbuf,
    pub loop_cnt_current: u16,
}

const _: () = assert!(core::mem::size_of::<netif>() == 112);

#[inline]
pub unsafe fn netif_is_up(n: *const netif) -> bool {
    !n.is_null() && ((*n).flags & NETIF_FLAG_UP) != 0
}

#[inline]
pub unsafe fn netif_is_link_up(n: *const netif) -> bool {
    !n.is_null() && ((*n).flags & NETIF_FLAG_LINK_UP) != 0
}

// ---------------------------------------------------------------------------
// Protocol headers / PCBs (protocol crates)
// ---------------------------------------------------------------------------

#[repr(C, packed)]
pub struct ip_hdr {
    pub v_hl: u8,
    pub tos: u8,
    pub len: u16,
    pub id: u16,
    pub offset: u16,
    pub ttl: u8,
    pub proto: u8,
    pub chksum: u16,
    pub src: u32,
    pub dest: u32,
}

const _: () = assert!(core::mem::size_of::<ip_hdr>() == 20);

pub unsafe fn iph_hl_bytes(ip: *const ip_hdr) -> u16 {
    (((*ip).v_hl & 0x0f) as u16) * 4
}

#[repr(C, packed)]
pub struct icmp_echo_hdr {
    pub type_: u8,
    pub code: u8,
    pub chksum: u16,
    pub id: u16,
    pub seqno: u16,
}

const _: () = assert!(core::mem::size_of::<icmp_echo_hdr>() == 8);

pub const ICMP_ER: u8 = 0;
pub const ICMP_ECHO: u8 = 8;
pub const IP_PROTO_ICMP: u8 = 1;
pub const TCP_WRITE_FLAG_COPY: u8 = 0x01;

pub enum raw_pcb {}
pub enum udp_pcb {}
pub enum tcp_pcb {}

pub type raw_recv_fn =
    unsafe extern "C" fn(*mut c_void, *mut raw_pcb, *mut pbuf, *const ip_addr_t) -> u8;
pub type udp_recv_fn =
    unsafe extern "C" fn(*mut c_void, *mut udp_pcb, *mut pbuf, *const ip_addr_t, u16);
pub type tcp_recv_fn = unsafe extern "C" fn(*mut c_void, *mut tcp_pcb, *mut pbuf, err_t) -> err_t;
pub type tcp_sent_fn = unsafe extern "C" fn(*mut c_void, *mut tcp_pcb, u16) -> err_t;
pub type tcp_poll_fn = unsafe extern "C" fn(*mut c_void, *mut tcp_pcb) -> err_t;
pub type tcp_err_fn = unsafe extern "C" fn(*mut c_void, err_t);
pub type tcp_connected_fn = unsafe extern "C" fn(*mut c_void, *mut tcp_pcb, err_t) -> err_t;
pub type tcp_accept_fn = unsafe extern "C" fn(*mut c_void, *mut tcp_pcb, err_t) -> err_t;
pub type dns_found_callback =
    unsafe extern "C" fn(name: *const c_char, addr: *const ip_addr_t, arg: *mut c_void);

extern "C" {
    pub fn lwip_init();

    pub fn netif_add(
        netif: *mut netif,
        ipaddr: *const ip4_addr_t,
        netmask: *const ip4_addr_t,
        gw: *const ip4_addr_t,
        state: *mut c_void,
        init: Option<netif_init_fn>,
        input: Option<netif_input_fn>,
    ) -> *mut netif;
    pub fn netif_set_default(netif: *mut netif);
    pub fn netif_set_up(netif: *mut netif);
    pub fn netif_set_link_up(netif: *mut netif);
    pub fn netif_poll_all();
    pub fn netif_loop_output(netif: *mut netif, p: *mut pbuf) -> err_t;

    pub fn etharp_output(netif: *mut netif, p: *mut pbuf, ipaddr: *const ip4_addr_t) -> err_t;
    pub fn ethernet_input(p: *mut pbuf, netif: *mut netif) -> err_t;
    /* lwIP: `#define ip_input ip4_input` when IPv4-only. */
    pub fn ip4_input(p: *mut pbuf, inp: *mut netif) -> err_t;

    pub fn dhcp_start(netif: *mut netif) -> err_t;
    pub fn dhcp_supplied_address(netif: *const netif) -> u8;

    pub fn dns_gethostbyname_addrtype(
        hostname: *const c_char,
        addr: *mut ip_addr_t,
        found: Option<dns_found_callback>,
        callback_arg: *mut c_void,
        dns_addrtype: u8,
    ) -> err_t;

    pub fn sys_check_timeouts();

    pub fn raw_new(proto: u8) -> *mut raw_pcb;
    pub fn raw_recv(pcb: *mut raw_pcb, recv: Option<raw_recv_fn>, recv_arg: *mut c_void);
    pub fn raw_sendto(pcb: *mut raw_pcb, p: *mut pbuf, ipaddr: *const ip_addr_t) -> err_t;
    pub fn raw_remove(pcb: *mut raw_pcb);

    pub fn udp_new() -> *mut udp_pcb;
    pub fn udp_new_ip_type(ty: u8) -> *mut udp_pcb;
    pub fn udp_bind(pcb: *mut udp_pcb, ipaddr: *const ip_addr_t, port: u16) -> err_t;
    pub fn udp_recv(pcb: *mut udp_pcb, recv: Option<udp_recv_fn>, recv_arg: *mut c_void);
    pub fn udp_sendto(
        pcb: *mut udp_pcb,
        p: *mut pbuf,
        dst_ip: *const ip_addr_t,
        dst_port: u16,
    ) -> err_t;
    pub fn udp_remove(pcb: *mut udp_pcb);

    pub fn tcp_new_ip_type(ty: u8) -> *mut tcp_pcb;
    pub fn tcp_arg(pcb: *mut tcp_pcb, arg: *mut c_void);
    pub fn tcp_recv(pcb: *mut tcp_pcb, recv: Option<tcp_recv_fn>);
    pub fn tcp_sent(pcb: *mut tcp_pcb, sent: Option<tcp_sent_fn>);
    pub fn tcp_poll(pcb: *mut tcp_pcb, poll: Option<tcp_poll_fn>, interval: u8);
    pub fn tcp_err(pcb: *mut tcp_pcb, err: Option<tcp_err_fn>);
    pub fn tcp_bind(pcb: *mut tcp_pcb, ipaddr: *const ip_addr_t, port: u16) -> err_t;
    pub fn tcp_listen_with_backlog_and_err(
        pcb: *mut tcp_pcb,
        backlog: u8,
        err: *mut err_t,
    ) -> *mut tcp_pcb;
    pub fn tcp_accept(pcb: *mut tcp_pcb, accept: Option<tcp_accept_fn>);
    pub fn tcp_connect(
        pcb: *mut tcp_pcb,
        ipaddr: *const ip_addr_t,
        port: u16,
        connected: Option<tcp_connected_fn>,
    ) -> err_t;
    pub fn tcp_write(pcb: *mut tcp_pcb, dataptr: *const c_void, len: u16, apiflags: u8) -> err_t;
    pub fn tcp_output(pcb: *mut tcp_pcb) -> err_t;
    pub fn tcp_recved(pcb: *mut tcp_pcb, len: u16);
    pub fn tcp_close(pcb: *mut tcp_pcb) -> err_t;
    pub fn tcp_abort(pcb: *mut tcp_pcb);

    pub fn pbuf_alloc(layer: u32, length: u16, ty: u32) -> *mut pbuf;
    pub fn pbuf_free(p: *mut pbuf) -> u8;
    pub fn pbuf_take(buf: *mut pbuf, dataptr: *const c_void, len: u16) -> err_t;
    pub fn pbuf_add_header(p: *mut pbuf, header_size_increment: usize) -> u8;
    pub fn pbuf_remove_header(p: *mut pbuf, header_size: usize) -> u8;
    pub fn pbuf_copy_partial(p: *const pbuf, dataptr: *mut c_void, len: u16, offset: u16) -> u16;

    pub fn ip4addr_aton(cp: *const c_char, addr: *mut ip4_addr_t) -> i32;
    pub fn ip4addr_ntoa_r(addr: *const ip4_addr_t, buf: *mut c_char, buflen: i32) -> *mut c_char;
    pub fn inet_chksum(dataptr: *const c_void, len: u16) -> u16;
    pub fn lwip_htons(x: u16) -> u16;
}

#[inline]
pub unsafe fn lwip_ntohs(x: u16) -> u16 {
    lwip_htons(x)
}
