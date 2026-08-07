//! lwIP NO_SYS bridge — DHCPv4 over pluggable L2.
use core::ffi::{c_char, c_void};
use core::ptr;

use crate::{
    dhcp_start, dhcp_supplied_address, err_t, etharp_output, ethernet_input, ip4_addr, ip4_addr_t,
    ip4_input, ip4addr_ntoa_r, lwip_init, netif, netif_add, netif_is_link_up, netif_is_up,
    netif_loop_output, netif_poll_all, netif_set_default, netif_set_link_up, netif_set_up, pbuf,
    pbuf_alloc, pbuf_free, pbuf_take, pm_metal_net_ip_l2_mac_fn, pm_metal_net_ip_l2_open_fn,
    pm_metal_net_ip_l2_ops_t, pm_metal_net_ip_l2_poll_fn, pm_metal_net_ip_l2_rx_fn,
    pm_metal_net_ip_l2_tx_fn, sys_check_timeouts, ERR_ARG, ERR_BUF, ERR_IF, ERR_OK, ETH_HWADDR_LEN,
    NETIF_FLAG_BROADCAST, NETIF_FLAG_ETHARP, NETIF_FLAG_ETHERNET, NETIF_FLAG_LINK_UP, PBUF_POOL,
    PBUF_RAW,
};
use crate::coro::pm_metal_time_mono_us;

const IFNAME_MAX: usize = 8;
const MAX_IFS: usize = 4;
const BACKEND_MAX: usize = 24;
const TX_SCRATCH: usize = 1514;

static HOSTNAME: &[u8] = b"metal\0";

struct Iface {
    used: bool,
    netif: netif,
    name: [u8; IFNAME_MAX],
    backend: [u8; BACKEND_MAX],
    l2_open: pm_metal_net_ip_l2_open_fn,
    l2_mac: pm_metal_net_ip_l2_mac_fn,
    l2_tx: pm_metal_net_ip_l2_tx_fn,
    l2_poll: pm_metal_net_ip_l2_poll_fn,
    ip: [u8; 16],
    mask: [u8; 16],
    gw: [u8; 16],
    use_dhcp: bool,
}

static mut IFACES: [Iface; MAX_IFS] = unsafe { core::mem::zeroed() };

static mut IFACE_COUNT: u32 = 0;
static mut ETH_COUNT: u32 = 0;
static mut DEFAULT_IDX: i32 = -1;
static mut LWIP_INITED: bool = false;
static mut TX_BUF: [u8; TX_SCRATCH] = [0; TX_SCRATCH];

unsafe fn cstr_eq(a: *const c_char, b: &[u8]) -> bool {
    if a.is_null() || b.is_empty() {
        return false;
    }
    let mut i = 0;
    while i < b.len() {
        let ab = *a.add(i) as u8;
        let bb = b[i];
        if bb == 0 {
            return ab == 0;
        }
        if ab != bb {
            return false;
        }
        i += 1;
    }
    false
}

unsafe fn copy_cstr(dst: &mut [u8], src: *const c_char) {
    if src.is_null() || dst.is_empty() {
        return;
    }
    let mut i = 0;
    while i + 1 < dst.len() {
        let c = *src.add(i) as u8;
        dst[i] = c;
        if c == 0 {
            return;
        }
        i += 1;
    }
    dst[dst.len() - 1] = 0;
}

fn set_cstr(dst: &mut [u8], s: &[u8]) {
    let n = core::cmp::min(s.len(), dst.len().saturating_sub(1));
    dst[..n].copy_from_slice(&s[..n]);
    if n < dst.len() {
        dst[n] = 0;
    } else if !dst.is_empty() {
        dst[dst.len() - 1] = 0;
    }
}

fn set_eth_name(dst: &mut [u8], n: u32) {
    dst[0] = b'e';
    dst[1] = b't';
    dst[2] = b'h';
    dst[3] = b'0' + (n.min(9) as u8);
    dst[4] = 0;
}

unsafe fn store_ip4_ascii(dst: &mut [u8], addr: *const ip4_addr_t) {
    if addr.is_null() || dst.is_empty() {
        return;
    }
    let p = ip4addr_ntoa_r(addr, dst.as_mut_ptr() as *mut c_char, dst.len() as i32);
    if p.is_null() {
        dst[0] = 0;
    }
}

unsafe fn sync_iface_cfg(mif: *mut Iface) {
    if mif.is_null() || !(*mif).used {
        return;
    }
    if (*mif).use_dhcp && dhcp_supplied_address(&(*mif).netif) == 0 {
        set_cstr(&mut (*mif).ip, b"0.0.0.0");
        set_cstr(&mut (*mif).mask, b"0.0.0.0");
        set_cstr(&mut (*mif).gw, b"0.0.0.0");
        return;
    }
    store_ip4_ascii(&mut (*mif).ip, &(*mif).netif.ip_addr);
    store_ip4_ascii(&mut (*mif).mask, &(*mif).netif.netmask);
    store_ip4_ascii(&mut (*mif).gw, &(*mif).netif.gw);
}

unsafe extern "C" fn metal_link_output(netif: *mut netif, p: *mut pbuf) -> err_t {
    if p.is_null() {
        return ERR_ARG;
    }
    let tot = (*p).tot_len as u32;
    if tot == 0 || tot as usize > TX_SCRATCH {
        return ERR_BUF;
    }
    let mut off = 0usize;
    let mut q = p;
    while !q.is_null() {
        let len = (*q).len as usize;
        ptr::copy_nonoverlapping(
            (*q).payload as *const u8,
            core::ptr::addr_of_mut!(TX_BUF).cast::<u8>().add(off),
            len,
        );
        off += len;
        q = (*q).next;
    }
    let mif = (*netif).state as *mut Iface;
    if mif.is_null() {
        return ERR_IF;
    }
    let Some(tx) = (*mif).l2_tx else {
        return ERR_IF;
    };
    if tx(core::ptr::addr_of!(TX_BUF).cast::<c_void>(), tot) != 0 {
        ERR_IF
    } else {
        ERR_OK
    }
}

unsafe extern "C" fn metal_netif_init(netif: *mut netif) -> err_t {
    let mif = (*netif).state as *mut Iface;
    if mif.is_null() {
        return ERR_IF;
    }
    let Some(mac_fn) = (*mif).l2_mac else {
        return ERR_IF;
    };
    let mac = mac_fn();
    if mac.is_null() {
        return ERR_IF;
    }
    (*netif).hwaddr_len = ETH_HWADDR_LEN;
    ptr::copy_nonoverlapping(mac, (*netif).hwaddr.as_mut_ptr(), 6);
    (*netif).mtu = 1500;
    (*netif).flags =
        NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
    (*netif).output = Some(etharp_output);
    (*netif).linkoutput = Some(metal_link_output);
    (*netif).hostname = HOSTNAME.as_ptr() as *const c_char;
    ERR_OK
}

unsafe extern "C" fn metal_loop_output_ipv4(
    netif: *mut netif,
    p: *mut pbuf,
    _addr: *const ip4_addr_t,
) -> err_t {
    netif_loop_output(netif, p)
}

unsafe extern "C" fn metal_loop_netif_init(netif: *mut netif) -> err_t {
    if netif.is_null() {
        return ERR_IF;
    }
    (*netif).name = [b'l' as c_char, b'o' as c_char];
    (*netif).mtu = 65535;
    (*netif).flags = NETIF_FLAG_LINK_UP;
    (*netif).hwaddr = [0; 6];
    (*netif).hwaddr_len = ETH_HWADDR_LEN;
    (*netif).output = Some(metal_loop_output_ipv4);
    (*netif).linkoutput = None;
    ERR_OK
}

unsafe extern "C" fn metal_on_frame(ctx: *mut c_void, frame: *const u8, len: u32) {
    let mif = ctx as *mut Iface;
    if mif.is_null() || frame.is_null() || len == 0 {
        return;
    }
    let p = pbuf_alloc(PBUF_RAW, len as u16, PBUF_POOL);
    if p.is_null() {
        return;
    }
    if pbuf_take(p, frame as *const c_void, len as u16) != ERR_OK {
        pbuf_free(p);
        return;
    }
    if let Some(input) = (*mif).netif.input {
        if input(p, &mut (*mif).netif) != ERR_OK {
            pbuf_free(p);
        }
    } else {
        pbuf_free(p);
    }
}

unsafe fn lwip_ensure() -> i32 {
    if !LWIP_INITED {
        lwip_init();
        LWIP_INITED = true;
    }
    0
}

unsafe fn iface_by_name(name: *const c_char) -> *mut Iface {
    let name = if name.is_null() {
        b"eth0\0".as_ptr() as *const c_char
    } else {
        name
    };
    for i in 0..MAX_IFS {
        let mif = &mut IFACES[i] as *mut Iface;
        if (*mif).used && cstr_eq(name, &(*mif).name) {
            return mif;
        }
    }
    ptr::null_mut()
}

fn write_hex_byte(dst: &mut [u8], off: &mut usize, b: u8) {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    if *off + 2 > dst.len() {
        return;
    }
    dst[*off] = HEX[(b >> 4) as usize];
    dst[*off + 1] = HEX[(b & 0xf) as usize];
    *off += 2;
}

fn write_bytes(dst: &mut [u8], off: &mut usize, s: &[u8]) {
    for &c in s {
        if c == 0 || *off >= dst.len() {
            break;
        }
        dst[*off] = c;
        *off += 1;
    }
}

/// lwIP `sys_now` (ms) — Metal mono clock.
#[no_mangle]
pub unsafe extern "C" fn sys_now() -> u32 {
    (pm_metal_time_mono_us() / 1000) as u32
}

pub unsafe fn l2_start(backend: *const c_char, ops: *const pm_metal_net_ip_l2_ops_t) -> i32 {
    if backend.is_null() || ops.is_null() {
        return -1;
    }
    let ops = &*ops;
    if ops.open.is_none() || ops.mac.is_none() || ops.tx.is_none() || ops.poll.is_none() {
        return -1;
    }
    if lwip_ensure() != 0 {
        return -1;
    }

    let mut idx = 0usize;
    while idx < MAX_IFS {
        if !IFACES[idx].used {
            break;
        }
        if cstr_eq(backend, &IFACES[idx].backend) {
            return 0;
        }
        idx += 1;
    }
    if idx >= MAX_IFS {
        return -1;
    }

    let mif = &mut IFACES[idx] as *mut Iface;
    ptr::write_bytes(mif as *mut u8, 0, core::mem::size_of::<Iface>());
    set_eth_name(&mut (*mif).name, ETH_COUNT);
    copy_cstr(&mut (*mif).backend, backend);
    (*mif).l2_open = ops.open;
    (*mif).l2_mac = ops.mac;
    (*mif).l2_tx = ops.tx;
    (*mif).l2_poll = ops.poll;
    (*mif).use_dhcp = true;

    let mut hwmac = [0u8; 6];
    if ops.open.unwrap()(hwmac.as_mut_ptr()) != 0 {
        return -1;
    }

    let zero = ip4_addr(0, 0, 0, 0);
    set_cstr(&mut (*mif).ip, b"0.0.0.0");
    if netif_add(
        &mut (*mif).netif,
        &zero,
        &zero,
        &zero,
        mif as *mut c_void,
        Some(metal_netif_init),
        Some(ethernet_input),
    )
    .is_null()
    {
        return -1;
    }

    (*mif).netif.hwaddr = hwmac;
    netif_set_up(&mut (*mif).netif);
    (*mif).netif.hostname = HOSTNAME.as_ptr() as *const c_char;

    if DEFAULT_IDX < 0
        || (IFACES[DEFAULT_IDX as usize].used
            && cstr_eq(
                b"loopback\0".as_ptr() as *const c_char,
                &IFACES[DEFAULT_IDX as usize].backend,
            ))
    {
        netif_set_default(&mut (*mif).netif);
        DEFAULT_IDX = idx as i32;
    }

    if dhcp_start(&mut (*mif).netif) != ERR_OK {
        return -1;
    }

    (*mif).used = true;
    ETH_COUNT += 1;
    IFACE_COUNT += 1;
    0
}

pub unsafe fn loopback_start() -> i32 {
    for i in 0..MAX_IFS {
        if IFACES[i].used && cstr_eq(b"lo\0".as_ptr() as *const c_char, &IFACES[i].name) {
            return 0;
        }
    }
    let mut idx = 0usize;
    while idx < MAX_IFS {
        if !IFACES[idx].used {
            break;
        }
        idx += 1;
    }
    if idx >= MAX_IFS {
        return -1;
    }
    if lwip_ensure() != 0 {
        return -1;
    }

    let mif = &mut IFACES[idx] as *mut Iface;
    ptr::write_bytes(mif as *mut u8, 0, core::mem::size_of::<Iface>());
    set_cstr(&mut (*mif).name, b"lo");
    set_cstr(&mut (*mif).backend, b"loopback");
    (*mif).use_dhcp = false;

    let ip = ip4_addr(127, 0, 0, 1);
    let nm = ip4_addr(255, 0, 0, 0);
    let gw = ip4_addr(127, 0, 0, 1);
    if netif_add(
        &mut (*mif).netif,
        &ip,
        &nm,
        &gw,
        mif as *mut c_void,
        Some(metal_loop_netif_init),
        Some(ip4_input),
    )
    .is_null()
    {
        return -1;
    }

    netif_set_link_up(&mut (*mif).netif);
    netif_set_up(&mut (*mif).netif);
    if DEFAULT_IDX < 0 {
        netif_set_default(&mut (*mif).netif);
        DEFAULT_IDX = idx as i32;
    }
    store_ip4_ascii(&mut (*mif).ip, &ip);
    store_ip4_ascii(&mut (*mif).mask, &nm);
    store_ip4_ascii(&mut (*mif).gw, &gw);
    (*mif).used = true;
    IFACE_COUNT += 1;
    0
}

pub unsafe fn poll() {
    if !LWIP_INITED {
        return;
    }
    for i in 0..MAX_IFS {
        if !IFACES[i].used {
            continue;
        }
        if let Some(poll_fn) = IFACES[i].l2_poll {
            let rx: pm_metal_net_ip_l2_rx_fn = Some(metal_on_frame);
            poll_fn(rx, &mut IFACES[i] as *mut Iface as *mut c_void);
        }
    }
    netif_poll_all();
    sys_check_timeouts();
    for i in 0..MAX_IFS {
        if IFACES[i].used {
            sync_iface_cfg(&mut IFACES[i]);
        }
    }
}

pub unsafe fn if_count() -> u32 {
    IFACE_COUNT
}

pub unsafe fn if_status_index(index: u32, dest: *mut c_char, dest_cap: u32) -> i32 {
    if dest.is_null() || dest_cap == 0 || index >= IFACE_COUNT {
        return -1;
    }
    let mut n = 0u32;
    let mut mif: *mut Iface = ptr::null_mut();
    for i in 0..MAX_IFS {
        if !IFACES[i].used {
            continue;
        }
        if n == index {
            mif = &mut IFACES[i];
            break;
        }
        n += 1;
    }
    if mif.is_null() {
        return -1;
    }
    sync_iface_cfg(mif);
    let up = netif_is_link_up(&(*mif).netif) && netif_is_up(&(*mif).netif);
    let mut buf = [0u8; 160];
    let mut off = 0usize;
    write_bytes(&mut buf, &mut off, &(*mif).name);
    write_bytes(&mut buf, &mut off, b" ");
    write_bytes(&mut buf, &mut off, &(*mif).ip);
    write_bytes(&mut buf, &mut off, b"/");
    write_bytes(&mut buf, &mut off, &(*mif).mask);
    write_bytes(&mut buf, &mut off, b" gw ");
    write_bytes(&mut buf, &mut off, &(*mif).gw);
    write_bytes(&mut buf, &mut off, if up { b" up" } else { b" down" });
    write_bytes(&mut buf, &mut off, b" mac ");
    for i in 0..6 {
        if i > 0 {
            write_bytes(&mut buf, &mut off, b":");
        }
        write_hex_byte(&mut buf, &mut off, (*mif).netif.hwaddr[i]);
    }
    let cap = dest_cap as usize;
    let n = core::cmp::min(off, cap.saturating_sub(1));
    ptr::copy_nonoverlapping(buf.as_ptr(), dest as *mut u8, n);
    *dest.add(n) = 0;
    n as i32
}

pub unsafe fn if_dhcp_ready(ifname: *const c_char, ip_out: *mut c_char, ip_cap: u32) -> i32 {
    let mif = iface_by_name(ifname);
    if mif.is_null() || !(*mif).used || !(*mif).use_dhcp {
        return -1;
    }
    sync_iface_cfg(mif);
    if dhcp_supplied_address(&(*mif).netif) == 0 {
        return 0;
    }
    if !ip_out.is_null() && ip_cap > 0 {
        let mut i = 0usize;
        let cap = ip_cap as usize;
        while i + 1 < cap && (*mif).ip[i] != 0 {
            *ip_out.add(i) = (*mif).ip[i] as c_char;
            i += 1;
        }
        *ip_out.add(i) = 0;
    }
    1
}
