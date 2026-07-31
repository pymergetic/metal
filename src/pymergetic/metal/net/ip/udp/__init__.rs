//! Async UDP bind/sendto/recvfrom under net.ip.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;
use core::ptr;

use pymergetic_metal_async as _;
use pymergetic_metal_net_ip as ip;
use pymergetic_metal_net_ip::coro::{self, Child};
use pymergetic_metal_rt as _;

const SOCK_MAX: usize = 8;
const RX_MAX: usize = 2048;
const IO_US: u64 = 10_000_000;

struct Sock {
    used: bool,
    pcb: *mut ip::udp_pcb,
    rx: [u8; RX_MAX],
    rx_len: u16,
    rx_addr: ip::ip_addr_t,
    rx_port: u16,
    have_rx: bool,
}

static mut SOCKS: [Sock; SOCK_MAX] = unsafe { core::mem::zeroed() };

#[repr(C)]
struct IoFrame {
    sock_h: u32,
    child_h: u32,
    deadline: u64,
    buf: *mut u8,
    len: u32,
    addr: *mut ip::ip_addr_t,
    port: *mut u16,
    is_send: u32,
    dst: ip::ip_addr_t,
    dst_port: u16,
}

unsafe fn sock_slot(h: u32) -> *mut Sock {
    if h == 0 || (h as usize) > SOCK_MAX {
        return ptr::null_mut();
    }
    let s = &mut SOCKS[(h as usize) - 1];
    if !s.used {
        return ptr::null_mut();
    }
    s
}

unsafe extern "C" fn udp_recv_cb(
    arg: *mut c_void,
    _pcb: *mut ip::udp_pcb,
    p: *mut ip::pbuf,
    addr: *const ip::ip_addr_t,
    port: u16,
) {
    let h = arg as usize as u32;
    let s = sock_slot(h);
    if s.is_null() {
        if !p.is_null() {
            ip::pbuf_free(p);
        }
        return;
    }
    if p.is_null() {
        return;
    }
    let n = core::cmp::min((*p).tot_len as usize, RX_MAX) as u16;
    let got = ip::pbuf_copy_partial(p, (*s).rx.as_mut_ptr() as *mut c_void, n, 0);
    ip::pbuf_free(p);
    if got == 0 {
        return;
    }
    (*s).rx_len = got;
    if !addr.is_null() {
        (*s).rx_addr = *addr;
    }
    (*s).rx_port = port;
    (*s).have_rx = true;
}

/// Bind UDP. `port == 0` requests an ephemeral port. Returns sock handle.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_udp_bind(port: u16) -> u32 {
    let mut slot = None;
    for i in 0..SOCK_MAX {
        if !SOCKS[i].used {
            slot = Some(i);
            break;
        }
    }
    let Some(i) = slot else {
        return 0;
    };
    let pcb = ip::udp_new_ip_type(ip::IPADDR_TYPE_V4);
    if pcb.is_null() {
        return 0;
    }
    if ip::udp_bind(pcb, &ip::IP_ADDR_ANY, port) != ip::ERR_OK {
        ip::udp_remove(pcb);
        return 0;
    }
    let h = (i + 1) as u32;
    SOCKS[i] = Sock {
        used: true,
        pcb,
        rx: [0; RX_MAX],
        rx_len: 0,
        rx_addr: ip::IP_ADDR_ANY,
        rx_port: 0,
        have_rx: false,
    };
    ip::udp_recv(pcb, Some(udp_recv_cb), h as usize as *mut c_void);
    h
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_udp_close(sock_h: u32) {
    let s = sock_slot(sock_h);
    if s.is_null() {
        return;
    }
    if !(*s).pcb.is_null() {
        ip::udp_remove((*s).pcb);
        (*s).pcb = ptr::null_mut();
    }
    (*s).used = false;
}

unsafe fn start_sleep(self_h: u32, child_h: &mut u32) -> u32 {
    match coro::start_sleep(self_h, child_h, 2000) {
        None => coro::ERROR,
        Some(st) => st,
    }
}

unsafe extern "C" fn io_step(self_h: u32) -> u32 {
    let f = coro::frame::<IoFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }
    if (*f).child_h != 0 {
        match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => return coro::WAITING,
            Child::Done(_) => {}
            Child::Failed => return coro::ERROR,
        }
    }
    ip::pm_metal_net_ip_poll();
    let s = sock_slot((*f).sock_h);
    if s.is_null() {
        return coro::ERROR;
    }

    if (*f).is_send != 0 {
        let p = ip::pbuf_alloc(ip::PBUF_TRANSPORT, (*f).len as u16, ip::PBUF_RAM);
        if p.is_null() {
            return coro::ERROR;
        }
        if ip::pbuf_take(p, (*f).buf as *const c_void, (*f).len as u16) != ip::ERR_OK {
            ip::pbuf_free(p);
            return coro::ERROR;
        }
        let err = ip::udp_sendto((*s).pcb, p, &(*f).dst, (*f).dst_port);
        ip::pbuf_free(p);
        if err != ip::ERR_OK {
            return coro::ERROR;
        }
        coro::pm_metal_async_set_result_u32(self_h, (*f).len);
        return coro::DONE;
    }

    if (*s).have_rx {
        let n = core::cmp::min((*s).rx_len as u32, (*f).len);
        ptr::copy_nonoverlapping((*s).rx.as_ptr(), (*f).buf, n as usize);
        if !(*f).addr.is_null() {
            *(*f).addr = (*s).rx_addr;
        }
        if !(*f).port.is_null() {
            *(*f).port = (*s).rx_port;
        }
        (*s).have_rx = false;
        (*s).rx_len = 0;
        coro::pm_metal_async_set_result_u32(self_h, n);
        return coro::DONE;
    }
    if coro::pm_metal_time_mono_us() >= (*f).deadline {
        return coro::ERROR;
    }
    start_sleep(self_h, &mut (*f).child_h)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_udp_sendto(
    sock_h: u32,
    buf: *const u8,
    len: u32,
    addr: *const ip::ip_addr_t,
    port: u16,
) -> u32 {
    if sock_slot(sock_h).is_null() || buf.is_null() || len == 0 || addr.is_null() || port == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<IoFrame>(io_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<IoFrame>(h);
    (*f).sock_h = sock_h;
    (*f).child_h = 0;
    (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
    (*f).buf = buf as *mut u8;
    (*f).len = len;
    (*f).addr = ptr::null_mut();
    (*f).port = ptr::null_mut();
    (*f).is_send = 1;
    (*f).dst = *addr;
    (*f).dst_port = port;
    h
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_udp_recvfrom(
    sock_h: u32,
    buf: *mut u8,
    cap: u32,
    addr_out: *mut ip::ip_addr_t,
    port_out: *mut u16,
) -> u32 {
    if sock_slot(sock_h).is_null() || buf.is_null() || cap == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<IoFrame>(io_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<IoFrame>(h);
    (*f).sock_h = sock_h;
    (*f).child_h = 0;
    (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
    (*f).buf = buf;
    (*f).len = cap;
    (*f).addr = addr_out;
    (*f).port = port_out;
    (*f).is_send = 0;
    (*f).dst = ip::IP_ADDR_ANY;
    (*f).dst_port = 0;
    h
}
