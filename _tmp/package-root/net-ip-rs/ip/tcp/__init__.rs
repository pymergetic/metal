//! Opaque byte stream under net.ip: TCP + optional SSL.
//!
//! **Open** (SSL only here): [`pm_metal_net_ip_tcp_connect`] (`ssl_opts`),
//! [`pm_metal_net_ip_tcp_listen`], [`pm_metal_net_ip_tcp_accept`] (`creds_h`).
//!
//! **I/O** (do not care what wrapped the link): [`pm_metal_net_ip_tcp_read`] /
//! [`pm_metal_net_ip_tcp_write`] / [`pm_metal_net_ip_tcp_close`]. Clear or SSL
//! is attached at open; callers never branch on TLS for byte I/O.
//!
//! **Sync poll**: [`pm_metal_net_ip_tcp_try_read`] /
//! [`pm_metal_net_ip_tcp_try_write`].
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};
use core::ptr;

use pymergetic_metal_async as _;
use pymergetic_metal_net_dns as dns;
use pymergetic_metal_net_ip as ip;
use pymergetic_metal_net_ip::coro::{self, Child};
use pymergetic_metal_net_ip::ssl;
use pymergetic_metal_rt as _;

const CONN_MAX: usize = 16;
const LISTEN_MAX: usize = 4;
const ACCEPT_Q: usize = 4;
const RX_MAX: usize = 4096;
const HOST_MAX: usize = 128;
const CONNECT_US: u64 = 10_000_000;
const IO_US: u64 = 30_000_000;
const BACKLOG: u8 = 4;

/// Optional SSL takeover for [`pm_metal_net_ip_tcp_connect`].
/// `insecure != 0` skips peer cert verify (default verify on).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_metal_net_ip_tcp_ssl_opts_t {
    pub sni: *const c_char,
    pub insecure: i32,
    pub ca_pem: *const u8,
    pub ca_pem_len: u32,
}

struct Conn {
    used: bool,
    pcb: *mut ip::tcp_pcb,
    connected: bool,
    err: bool,
    remote_closed: bool,
    rx: [u8; RX_MAX],
    rx_len: u32,
    rx_off: u32,
    ssl_h: u32,
    wire: ssl::Wire,
}

struct Listen {
    used: bool,
    pcb: *mut ip::tcp_pcb,
    q: [u32; ACCEPT_Q],
    q_n: usize,
}

static mut CONNS: [Conn; CONN_MAX] = unsafe { core::mem::zeroed() };
static mut LISTENS: [Listen; LISTEN_MAX] = unsafe { core::mem::zeroed() };

const PHASE_DNS: u32 = 0;
const PHASE_DNS_WAIT: u32 = 1;
const PHASE_TCP: u32 = 2;
const PHASE_TCP_WAIT: u32 = 3;
const PHASE_SSL: u32 = 4;
const PHASE_SSL_WAIT: u32 = 5;
const PHASE_DONE: u32 = 6;

#[repr(C)]
struct ConnectFrame {
    phase: u32,
    host: [u8; HOST_MAX],
    port: u16,
    child_h: u32,
    deadline: u64,
    server: ip::ip_addr_t,
    conn_h: u32,
    use_ssl: u32,
    insecure: i32,
    sni: [u8; HOST_MAX],
    ca_pem: *const u8,
    ca_pem_len: u32,
}

#[repr(C)]
struct AcceptFrame {
    phase: u32,
    listen_h: u32,
    creds_h: u32,
    child_h: u32,
    conn_h: u32,
    deadline: u64,
}

#[repr(C)]
struct IoFrame {
    phase: u32,
    conn_h: u32,
    child_h: u32,
    deadline: u64,
    buf: *mut u8,
    len: u32,
    done: u32,
    is_write: u32,
}

unsafe fn conn_slot(h: u32) -> *mut Conn {
    if h == 0 || (h as usize) > CONN_MAX {
        return ptr::null_mut();
    }
    let c = &mut CONNS[(h as usize) - 1];
    if !c.used {
        return ptr::null_mut();
    }
    c
}

unsafe fn listen_slot(h: u32) -> *mut Listen {
    if h == 0 || (h as usize) > LISTEN_MAX {
        return ptr::null_mut();
    }
    let l = &mut LISTENS[(h as usize) - 1];
    if !l.used {
        return ptr::null_mut();
    }
    l
}

unsafe fn conn_alloc() -> u32 {
    for i in 0..CONN_MAX {
        if !CONNS[i].used {
            CONNS[i] = Conn {
                used: true,
                pcb: ptr::null_mut(),
                connected: false,
                err: false,
                remote_closed: false,
                rx: [0; RX_MAX],
                rx_len: 0,
                rx_off: 0,
                ssl_h: 0,
                wire: ssl::Wire {
                    buf: [0; ssl::PM_METAL_NET_IP_SSL_WIRE_MAX],
                    len: 0,
                    off: 0,
                },
            };
            return (i + 1) as u32;
        }
    }
    0
}

unsafe fn conn_free(h: u32) {
    let c = conn_slot(h);
    if c.is_null() {
        return;
    }
    if (*c).ssl_h != 0 {
        ssl::close((*c).ssl_h);
        (*c).ssl_h = 0;
    }
    if !(*c).pcb.is_null() {
        ip::tcp_arg((*c).pcb, ptr::null_mut());
        ip::tcp_recv((*c).pcb, None);
        ip::tcp_err((*c).pcb, None);
        if ip::tcp_close((*c).pcb) != ip::ERR_OK {
            ip::tcp_abort((*c).pcb);
        }
        (*c).pcb = ptr::null_mut();
    }
    (*c).used = false;
}

unsafe fn feed_rx(c: *mut Conn, p: *mut ip::pbuf) {
    if p.is_null() {
        (*c).remote_closed = true;
        return;
    }
    let mut offset = 0u16;
    while offset < (*p).tot_len {
        let room = RX_MAX as u32 - (*c).rx_len;
        if room == 0 {
            break;
        }
        let n = core::cmp::min(((*p).tot_len - offset) as u32, room) as u16;
        let got = ip::pbuf_copy_partial(
            p,
            (*c).rx.as_mut_ptr().add((*c).rx_len as usize) as *mut c_void,
            n,
            offset,
        );
        if got == 0 {
            break;
        }
        (*c).rx_len += got as u32;
        offset += got;
        ip::tcp_recved((*c).pcb, got);
    }
    ip::pbuf_free(p);
}

unsafe extern "C" fn conn_recv(
    arg: *mut c_void,
    _pcb: *mut ip::tcp_pcb,
    p: *mut ip::pbuf,
    err: ip::err_t,
) -> ip::err_t {
    let h = arg as usize as u32;
    let c = conn_slot(h);
    if c.is_null() {
        if !p.is_null() {
            ip::pbuf_free(p);
        }
        return ip::ERR_OK;
    }
    if err != ip::ERR_OK {
        (*c).err = true;
        if !p.is_null() {
            ip::pbuf_free(p);
        }
        return ip::ERR_OK;
    }
    if (*c).ssl_h != 0 {
        /* Wire bytes for mbedtls bio. */
        if p.is_null() {
            (*c).remote_closed = true;
            return ip::ERR_OK;
        }
        let mut offset = 0u16;
        while offset < (*p).tot_len {
            let room = ssl::PM_METAL_NET_IP_SSL_WIRE_MAX as u32 - (*c).wire.len;
            if room == 0 {
                break;
            }
            let n = core::cmp::min(((*p).tot_len - offset) as u32, room) as u16;
            let got = ip::pbuf_copy_partial(
                p,
                (*c).wire.buf.as_mut_ptr().add((*c).wire.len as usize) as *mut c_void,
                n,
                offset,
            );
            if got == 0 {
                break;
            }
            (*c).wire.len += got as u32;
            offset += got;
            ip::tcp_recved((*c).pcb, got);
        }
        ip::pbuf_free(p);
        return ip::ERR_OK;
    }
    feed_rx(c, p);
    ip::ERR_OK
}

unsafe extern "C" fn conn_err(arg: *mut c_void, _err: ip::err_t) {
    let h = arg as usize as u32;
    let c = conn_slot(h);
    if !c.is_null() {
        (*c).err = true;
        (*c).pcb = ptr::null_mut();
    }
}

unsafe extern "C" fn conn_connected(
    arg: *mut c_void,
    _pcb: *mut ip::tcp_pcb,
    err: ip::err_t,
) -> ip::err_t {
    let h = arg as usize as u32;
    let c = conn_slot(h);
    if c.is_null() {
        return ip::ERR_ARG;
    }
    if err != ip::ERR_OK {
        (*c).err = true;
    } else {
        (*c).connected = true;
    }
    ip::ERR_OK
}

unsafe extern "C" fn listen_accept(
    arg: *mut c_void,
    newpcb: *mut ip::tcp_pcb,
    err: ip::err_t,
) -> ip::err_t {
    let lh = arg as usize as u32;
    let l = listen_slot(lh);
    if l.is_null() || err != ip::ERR_OK || newpcb.is_null() {
        if !newpcb.is_null() {
            ip::tcp_abort(newpcb);
        }
        return ip::ERR_ABRT;
    }
    if (*l).q_n >= ACCEPT_Q {
        ip::tcp_abort(newpcb);
        return ip::ERR_ABRT;
    }
    let ch = conn_alloc();
    if ch == 0 {
        ip::tcp_abort(newpcb);
        return ip::ERR_ABRT;
    }
    let c = conn_slot(ch);
    (*c).pcb = newpcb;
    (*c).connected = true;
    ip::tcp_arg(newpcb, ch as usize as *mut c_void);
    ip::tcp_recv(newpcb, Some(conn_recv));
    ip::tcp_err(newpcb, Some(conn_err));
    (*l).q[(*l).q_n] = ch;
    (*l).q_n += 1;
    ip::ERR_OK
}

/* lwIP may not export ERR_ABRT as we named it — use -13 if needed */
const _: () = assert!(ip::ERR_ARG == -16);

unsafe fn raw_send(c: *mut Conn, data: *const u8, len: u32) -> i32 {
    if c.is_null() || (*c).pcb.is_null() || data.is_null() || len == 0 {
        return -1;
    }
    let n = core::cmp::min(len, 0xffff) as u16;
    let err = ip::tcp_write(
        (*c).pcb,
        data as *const c_void,
        n,
        ip::TCP_WRITE_FLAG_COPY,
    );
    if err == ip::ERR_MEM {
        return ssl::PM_METAL_NET_IP_SSL_WANT_WRITE;
    }
    if err != ip::ERR_OK {
        return -1;
    }
    let _ = ip::tcp_output((*c).pcb);
    n as i32
}

unsafe extern "C" fn bio_send(ctx: *mut c_void, buf: *const u8, len: usize) -> i32 {
    let c = conn_slot(ctx as usize as u32);
    if c.is_null() || buf.is_null() {
        return -1;
    }
    raw_send(c, buf, len as u32)
}

unsafe extern "C" fn bio_recv(ctx: *mut c_void, buf: *mut u8, len: usize) -> i32 {
    let c = conn_slot(ctx as usize as u32);
    if c.is_null() || buf.is_null() || len == 0 {
        return -1;
    }
    if (*c).wire.off < (*c).wire.len {
        let n = core::cmp::min(((*c).wire.len - (*c).wire.off) as usize, len);
        ptr::copy_nonoverlapping(
            (*c).wire.buf.as_ptr().add((*c).wire.off as usize),
            buf,
            n,
        );
        (*c).wire.off += n as u32;
        if (*c).wire.off >= (*c).wire.len {
            (*c).wire.off = 0;
            (*c).wire.len = 0;
        }
        return n as i32;
    }
    if (*c).remote_closed {
        return 0;
    }
    ssl::PM_METAL_NET_IP_SSL_WANT_READ
}

unsafe fn attach_client_ssl(f: *mut ConnectFrame) -> bool {
    let c = conn_slot((*f).conn_h);
    if c.is_null() {
        return false;
    }
    if (*f).ca_pem_len > 0 && !(*f).ca_pem.is_null() {
        if ssl::pm_metal_net_ip_ssl_set_ca((*f).ca_pem, (*f).ca_pem_len) != 0 {
            return false;
        }
    }
    let sni = if (*f).sni[0] != 0 {
        (*f).sni.as_ptr() as *const c_char
    } else {
        (*f).host.as_ptr() as *const c_char
    };
    let h = ssl::wrap_client(
        (*f).conn_h as usize as *mut c_void,
        Some(bio_send),
        Some(bio_recv),
        sni,
        (*f).insecure,
    );
    if h == 0 {
        return false;
    }
    (*c).ssl_h = h;
    true
}

unsafe fn attach_server_ssl(conn_h: u32, creds_h: u32) -> bool {
    let c = conn_slot(conn_h);
    if c.is_null() || creds_h == 0 {
        return false;
    }
    let h = ssl::wrap_server(
        conn_h as usize as *mut c_void,
        Some(bio_send),
        Some(bio_recv),
        creds_h,
    );
    if h == 0 {
        return false;
    }
    (*c).ssl_h = h;
    true
}

unsafe fn start_sleep(self_h: u32, child_h: &mut u32) -> u32 {
    match coro::start_sleep(self_h, child_h, 2000) {
        None => coro::ERROR,
        Some(s) => s,
    }
}

unsafe fn poll_child(self_h: u32, child_h: &mut u32) -> Option<u32> {
    if *child_h == 0 {
        return None;
    }
    match coro::finish_child(self_h, child_h) {
        Child::Waiting => Some(coro::WAITING),
        Child::Done(_) => None,
        Child::Failed => Some(coro::ERROR),
    }
}

unsafe extern "C" fn connect_step(self_h: u32) -> u32 {
    let f = coro::frame::<ConnectFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }
    match (*f).phase {
        PHASE_DNS => {
            if let Some(addr) = ip::aton((*f).host.as_ptr() as *const c_char) {
                (*f).server = addr;
                (*f).phase = PHASE_TCP;
                return coro::PENDING;
            }
            (*f).child_h = dns::pm_metal_net_dns((*f).host.as_ptr() as *const c_char);
            if (*f).child_h == 0 {
                return coro::ERROR;
            }
            (*f).phase = PHASE_DNS_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        PHASE_DNS_WAIT => {
            let ok = match coro::finish_child(self_h, &mut (*f).child_h) {
                Child::Waiting => return coro::WAITING,
                Child::Done(r) => r,
                Child::Failed => 0,
            };
            let Some(addr) = (if ok != 0 { dns::last_addr() } else { None }) else {
                return coro::ERROR;
            };
            (*f).server = addr;
            (*f).phase = PHASE_TCP;
            coro::PENDING
        }
        PHASE_TCP => {
            let ch = conn_alloc();
            if ch == 0 {
                return coro::ERROR;
            }
            (*f).conn_h = ch;
            let c = conn_slot(ch);
            (*c).pcb = ip::tcp_new_ip_type(ip::IPADDR_TYPE_V4);
            if (*c).pcb.is_null() {
                conn_free(ch);
                return coro::ERROR;
            }
            ip::tcp_arg((*c).pcb, ch as usize as *mut c_void);
            ip::tcp_recv((*c).pcb, Some(conn_recv));
            ip::tcp_err((*c).pcb, Some(conn_err));
            if ip::tcp_connect((*c).pcb, &(*f).server, (*f).port, Some(conn_connected))
                != ip::ERR_OK
            {
                conn_free(ch);
                return coro::ERROR;
            }
            (*f).deadline = coro::pm_metal_time_mono_us() + CONNECT_US;
            (*f).phase = PHASE_TCP_WAIT;
            coro::PENDING
        }
        PHASE_TCP_WAIT => {
            if let Some(s) = poll_child(self_h, &mut (*f).child_h) {
                return s;
            }
            ip::pm_metal_net_ip_poll();
            let c = conn_slot((*f).conn_h);
            if c.is_null() || (*c).err {
                conn_free((*f).conn_h);
                return coro::ERROR;
            }
            if (*c).connected {
                if (*f).use_ssl != 0 {
                    (*f).phase = PHASE_SSL;
                } else {
                    (*f).phase = PHASE_DONE;
                }
                return coro::PENDING;
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                conn_free((*f).conn_h);
                return coro::ERROR;
            }
            start_sleep(self_h, &mut (*f).child_h)
        }
        PHASE_SSL => {
            if !attach_client_ssl(f) {
                conn_free((*f).conn_h);
                return coro::ERROR;
            }
            (*f).deadline = coro::pm_metal_time_mono_us() + CONNECT_US;
            (*f).phase = PHASE_SSL_WAIT;
            coro::PENDING
        }
        PHASE_SSL_WAIT => {
            if let Some(s) = poll_child(self_h, &mut (*f).child_h) {
                return s;
            }
            ip::pm_metal_net_ip_poll();
            let c = conn_slot((*f).conn_h);
            if c.is_null() || (*c).err {
                conn_free((*f).conn_h);
                return coro::ERROR;
            }
            let st = ssl::handshake_step((*c).ssl_h);
            if st == 0 {
                (*f).phase = PHASE_DONE;
                return coro::PENDING;
            }
            if st < 0 {
                conn_free((*f).conn_h);
                return coro::ERROR;
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                conn_free((*f).conn_h);
                return coro::ERROR;
            }
            start_sleep(self_h, &mut (*f).child_h)
        }
        PHASE_DONE => {
            coro::pm_metal_async_set_result_u32(self_h, (*f).conn_h);
            coro::DONE
        }
        _ => coro::ERROR,
    }
}

unsafe extern "C" fn accept_step(self_h: u32) -> u32 {
    let f = coro::frame::<AcceptFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }
    match (*f).phase {
        0 => {
            if let Some(s) = poll_child(self_h, &mut (*f).child_h) {
                return s;
            }
            ip::pm_metal_net_ip_poll();
            let l = listen_slot((*f).listen_h);
            if l.is_null() {
                return coro::ERROR;
            }
            if (*l).q_n > 0 {
                let ch = (*l).q[0];
                for i in 1..(*l).q_n {
                    (*l).q[i - 1] = (*l).q[i];
                }
                (*l).q_n -= 1;
                (*f).conn_h = ch;
                if (*f).creds_h != 0 {
                    if !attach_server_ssl(ch, (*f).creds_h) {
                        conn_free(ch);
                        return coro::ERROR;
                    }
                    (*f).deadline = coro::pm_metal_time_mono_us() + CONNECT_US;
                    (*f).phase = 1;
                    return coro::PENDING;
                }
                coro::pm_metal_async_set_result_u32(self_h, ch);
                return coro::DONE;
            }
            start_sleep(self_h, &mut (*f).child_h)
        }
        1 => {
            if let Some(s) = poll_child(self_h, &mut (*f).child_h) {
                return s;
            }
            ip::pm_metal_net_ip_poll();
            let c = conn_slot((*f).conn_h);
            if c.is_null() || (*c).err {
                conn_free((*f).conn_h);
                return coro::ERROR;
            }
            let st = ssl::handshake_step((*c).ssl_h);
            if st == 0 {
                coro::pm_metal_async_set_result_u32(self_h, (*f).conn_h);
                return coro::DONE;
            }
            if st < 0 || coro::pm_metal_time_mono_us() >= (*f).deadline {
                conn_free((*f).conn_h);
                return coro::ERROR;
            }
            start_sleep(self_h, &mut (*f).child_h)
        }
        _ => coro::ERROR,
    }
}

unsafe extern "C" fn io_step(self_h: u32) -> u32 {
    let f = coro::frame::<IoFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }
    if let Some(s) = poll_child(self_h, &mut (*f).child_h) {
        return s;
    }
    ip::pm_metal_net_ip_poll();
    let c = conn_slot((*f).conn_h);
    if c.is_null() || (*c).err {
        return coro::ERROR;
    }

    if (*f).is_write != 0 {
        let n = if (*c).ssl_h != 0 {
            ssl::write((*c).ssl_h, (*f).buf, (*f).len - (*f).done)
        } else {
            let left = (*f).len - (*f).done;
            let r = raw_send(c, (*f).buf.add((*f).done as usize), left);
            if r == ssl::PM_METAL_NET_IP_SSL_WANT_WRITE {
                r
            } else {
                r
            }
        };
        if n == ssl::PM_METAL_NET_IP_SSL_WANT_READ || n == ssl::PM_METAL_NET_IP_SSL_WANT_WRITE {
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                return coro::ERROR;
            }
            return start_sleep(self_h, &mut (*f).child_h);
        }
        if n <= 0 {
            return coro::ERROR;
        }
        (*f).done += n as u32;
        if (*f).done >= (*f).len {
            coro::pm_metal_async_set_result_u32(self_h, (*f).done);
            return coro::DONE;
        }
        return start_sleep(self_h, &mut (*f).child_h);
    }

    /* read */
    if (*c).ssl_h != 0 {
        let n = ssl::read((*c).ssl_h, (*f).buf, (*f).len);
        if n == ssl::PM_METAL_NET_IP_SSL_WANT_READ || n == ssl::PM_METAL_NET_IP_SSL_WANT_WRITE {
            if (*c).remote_closed {
                coro::pm_metal_async_set_result_u32(self_h, 0);
                return coro::DONE;
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                return coro::ERROR;
            }
            return start_sleep(self_h, &mut (*f).child_h);
        }
        if n < 0 {
            return coro::ERROR;
        }
        coro::pm_metal_async_set_result_u32(self_h, n as u32);
        return coro::DONE;
    }

    if (*c).rx_off < (*c).rx_len {
        let n = core::cmp::min(((*c).rx_len - (*c).rx_off) as u32, (*f).len);
        ptr::copy_nonoverlapping(
            (*c).rx.as_ptr().add((*c).rx_off as usize),
            (*f).buf,
            n as usize,
        );
        (*c).rx_off += n;
        if (*c).rx_off >= (*c).rx_len {
            (*c).rx_off = 0;
            (*c).rx_len = 0;
        }
        coro::pm_metal_async_set_result_u32(self_h, n);
        return coro::DONE;
    }
    if (*c).remote_closed {
        coro::pm_metal_async_set_result_u32(self_h, 0);
        return coro::DONE;
    }
    if coro::pm_metal_time_mono_us() >= (*f).deadline {
        return coro::ERROR;
    }
    start_sleep(self_h, &mut (*f).child_h)
}

/// DNS + TCP connect; optional SSL takeover. Result u32 = stream handle
/// (use [`pm_metal_net_ip_tcp_read`] / [`write`] / [`close`]).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_connect(
    host: *const c_char,
    port: u16,
    ssl_opts: *const pm_metal_net_ip_tcp_ssl_opts_t,
) -> u32 {
    if host.is_null() || port == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<ConnectFrame>(connect_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<ConnectFrame>(h);
    if !coro::copy_cstr(&mut (*f).host, host) {
        coro::pm_metal_async_coro_close(h);
        return 0;
    }
    (*f).phase = PHASE_DNS;
    (*f).port = port;
    (*f).child_h = 0;
    (*f).conn_h = 0;
    (*f).use_ssl = 0;
    (*f).insecure = 0;
    (*f).sni = [0; HOST_MAX];
    (*f).ca_pem = ptr::null();
    (*f).ca_pem_len = 0;
    if !ssl_opts.is_null() {
        (*f).use_ssl = 1;
        (*f).insecure = (*ssl_opts).insecure;
        (*f).ca_pem = (*ssl_opts).ca_pem;
        (*f).ca_pem_len = (*ssl_opts).ca_pem_len;
        let sni = if !(*ssl_opts).sni.is_null() {
            (*ssl_opts).sni
        } else {
            host
        };
        let _ = coro::copy_cstr(&mut (*f).sni, sni);
    }
    h
}

/// Bind + listen. Returns listen handle (0 on failure).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_listen(port: u16) -> u32 {
    if port == 0 {
        return 0;
    }
    let mut slot = 0usize;
    let mut found = false;
    for i in 0..LISTEN_MAX {
        if !LISTENS[i].used {
            slot = i;
            found = true;
            break;
        }
    }
    if !found {
        return 0;
    }
    let pcb = ip::tcp_new_ip_type(ip::IPADDR_TYPE_V4);
    if pcb.is_null() {
        return 0;
    }
    if ip::tcp_bind(pcb, &ip::IP_ADDR_ANY, port) != ip::ERR_OK {
        ip::tcp_abort(pcb);
        return 0;
    }
    let mut err: ip::err_t = ip::ERR_OK;
    let lpcb = ip::tcp_listen_with_backlog_and_err(pcb, BACKLOG, &mut err);
    if lpcb.is_null() || err != ip::ERR_OK {
        return 0;
    }
    let lh = (slot + 1) as u32;
    LISTENS[slot] = Listen {
        used: true,
        pcb: lpcb,
        q: [0; ACCEPT_Q],
        q_n: 0,
    };
    ip::tcp_arg(lpcb, lh as usize as *mut c_void);
    ip::tcp_accept(lpcb, Some(listen_accept));
    lh
}

/// Await next accepted stream. `creds_h == 0` cleartext; else SSL takeover.
/// Result u32 = stream handle (same I/O as connect).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_accept(listen_h: u32, creds_h: u32) -> u32 {
    if listen_slot(listen_h).is_null() {
        return 0;
    }
    let h = coro::coro_with_frame::<AcceptFrame>(accept_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<AcceptFrame>(h);
    (*f).phase = 0;
    (*f).listen_h = listen_h;
    (*f).creds_h = creds_h;
    (*f).child_h = 0;
    (*f).conn_h = 0;
    h
}

/// Opaque stream read (clear or SSL). Result u32 = bytes read (0 = EOF).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_read(stream_h: u32, buf: *mut u8, cap: u32) -> u32 {
    if conn_slot(stream_h).is_null() || buf.is_null() || cap == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<IoFrame>(io_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<IoFrame>(h);
    (*f).phase = 0;
    (*f).conn_h = stream_h;
    (*f).child_h = 0;
    (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
    (*f).buf = buf;
    (*f).len = cap;
    (*f).done = 0;
    (*f).is_write = 0;
    h
}

/// Opaque stream write (clear or SSL). Result u32 = bytes written.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_write(stream_h: u32, buf: *const u8, len: u32) -> u32 {
    if conn_slot(stream_h).is_null() || buf.is_null() || len == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<IoFrame>(io_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<IoFrame>(h);
    (*f).phase = 0;
    (*f).conn_h = stream_h;
    (*f).child_h = 0;
    (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
    (*f).buf = buf as *mut u8;
    (*f).len = len;
    (*f).done = 0;
    (*f).is_write = 1;
    h
}

/// Close opaque stream (tears down SSL session if attached).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_close(stream_h: u32) {
    conn_free(stream_h);
}

/// Sync non-blocking read (select/atomicio-style).
/// Returns bytes copied, `0` if empty (EAGAIN), or `u32::MAX` if closed/error.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_try_read(stream_h: u32, buf: *mut u8, cap: u32) -> u32 {
    if buf.is_null() || cap == 0 {
        return 0;
    }
    ip::pm_metal_net_ip_poll();
    let c = conn_slot(stream_h);
    if c.is_null() || (*c).err {
        return u32::MAX;
    }
    if (*c).ssl_h != 0 {
        let n = ssl::read((*c).ssl_h, buf, cap);
        if n == ssl::PM_METAL_NET_IP_SSL_WANT_READ || n == ssl::PM_METAL_NET_IP_SSL_WANT_WRITE {
            if (*c).remote_closed {
                return u32::MAX;
            }
            return 0;
        }
        if n < 0 {
            return u32::MAX;
        }
        if n == 0 {
            return u32::MAX;
        }
        return n as u32;
    }
    if (*c).rx_off < (*c).rx_len {
        let n = core::cmp::min(((*c).rx_len - (*c).rx_off) as u32, cap);
        ptr::copy_nonoverlapping(
            (*c).rx.as_ptr().add((*c).rx_off as usize),
            buf,
            n as usize,
        );
        (*c).rx_off += n;
        if (*c).rx_off >= (*c).rx_len {
            (*c).rx_off = 0;
            (*c).rx_len = 0;
        }
        return n;
    }
    if (*c).remote_closed {
        return u32::MAX;
    }
    0
}

/// Sync non-blocking write (select/atomicio-style).
/// Returns bytes sent, or `0` if would-block / error.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_try_write(
    stream_h: u32,
    buf: *const u8,
    len: u32,
) -> u32 {
    if buf.is_null() || len == 0 {
        return 0;
    }
    ip::pm_metal_net_ip_poll();
    let c = conn_slot(stream_h);
    if c.is_null() || (*c).err {
        return 0;
    }
    if (*c).ssl_h != 0 {
        let n = ssl::write((*c).ssl_h, buf as *mut u8, len);
        if n == ssl::PM_METAL_NET_IP_SSL_WANT_READ || n == ssl::PM_METAL_NET_IP_SSL_WANT_WRITE {
            return 0;
        }
        if n <= 0 {
            return 0;
        }
        return n as u32;
    }
    let r = raw_send(c, buf, len);
    if r == ssl::PM_METAL_NET_IP_SSL_WANT_WRITE || r <= 0 {
        return 0;
    }
    r as u32
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_tcp_listen_close(listen_h: u32) {
    let l = listen_slot(listen_h);
    if l.is_null() {
        return;
    }
    for i in 0..(*l).q_n {
        conn_free((*l).q[i]);
    }
    if !(*l).pcb.is_null() {
        ip::tcp_accept((*l).pcb, None);
        ip::tcp_arg((*l).pcb, ptr::null_mut());
        if ip::tcp_close((*l).pcb) != ip::ERR_OK {
            ip::tcp_abort((*l).pcb);
        }
        (*l).pcb = ptr::null_mut();
    }
    (*l).used = false;
    (*l).q_n = 0;
}
