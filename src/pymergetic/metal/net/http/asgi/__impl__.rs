//! pymergetic.metal.net.http.asgi — stackless HTTP/1.0 server on net.ip.
//!
//! Microdot-shaped: `route(method, path, body)` then `listen`. Park via the
//! same WAITING returns as ip (`accept == -2`, `recv == 0`). Default app is
//! GET * → `asgi`. Not a second HTTP client (that stays impl=c).

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use core::cell::UnsafeCell;
use core::ffi::c_void;
use core::ptr;

const SOCK_STREAM: i32 = 1;
const WAITING: i32 = 1;
const DONE: i32 = 2;
const ERROR: i32 = 4;
const ACCEPT_WAIT: i32 = -2;
const MAX_ROUTE: usize = 8;
const MAX_CONN: usize = 4;
const RX_MAX: usize = 1024;
const HDR_MAX: usize = 160;

#[repr(C)]
pub struct pm_util_mem_arena_t {
    _opaque: [u8; 0],
}

#[repr(C)]
struct pm_metal_async_coro_t {
    _opaque: [u8; 0],
}

#[repr(C)]
struct pm_metal_async_task_t {
    _opaque: [u8; 0],
}

unsafe extern "C" {
    fn pm_metal_net_ip_socket(kind: i32) -> i32;
    fn pm_metal_net_ip_close(fd: i32) -> i32;
    fn pm_metal_net_ip_bind(fd: i32, addr: u32, port: u16) -> i32;
    fn pm_metal_net_ip_listen(fd: i32, backlog: i32) -> i32;
    fn pm_metal_net_ip_accept(fd: i32) -> i32;
    fn pm_metal_net_ip_send(fd: i32, buf: *const u8, len: u32) -> i32;
    fn pm_metal_net_ip_recv(fd: i32, buf: *mut u8, len: u32) -> i32;
    fn pm_metal_async_coro_create(
        step: unsafe extern "C" fn(*mut pm_metal_async_coro_t) -> i32,
        frame_bytes: usize,
    ) -> *mut pm_metal_async_coro_t;
    fn pm_metal_async_create_task(coro: *mut pm_metal_async_coro_t) -> *mut pm_metal_async_task_t;
}

#[derive(Clone, Copy)]
struct Route {
    used: bool,
    method: [u8; 8],
    path: [u8; 80],
    body: [u8; 256],
    body_len: u32,
}

#[derive(Clone, Copy)]
struct Conn {
    used: bool,
    step: u32,
    fd: i32,
    rx: [u8; RX_MAX],
    rx_len: u32,
    snd_off: u32,
    hdr_len: u32,
    hdr: [u8; HDR_MAX],
    body: *const u8,
    body_len: u32,
}

struct Mut<T>(UnsafeCell<T>);
unsafe impl<T> Sync for Mut<T> {}

static ARENA: Mut<*mut pm_util_mem_arena_t> = Mut(UnsafeCell::new(ptr::null_mut()));
static LISTEN_FD: Mut<i32> = Mut(UnsafeCell::new(-1));
static ROUTES: Mut<[Route; MAX_ROUTE]> = Mut(UnsafeCell::new([Route {
    used: false,
    method: [0; 8],
    path: [0; 80],
    body: [0; 256],
    body_len: 0,
}; MAX_ROUTE]));
static CONNS: Mut<[Conn; MAX_CONN]> = Mut(UnsafeCell::new([Conn {
    used: false,
    step: 0,
    fd: -1,
    rx: [0; RX_MAX],
    rx_len: 0,
    snd_off: 0,
    hdr_len: 0,
    hdr: [0; HDR_MAX],
    body: ptr::null(),
    body_len: 0,
}; MAX_CONN]));
static DEFAULT_BODY: &[u8] = b"asgi";

unsafe fn routes() -> &'static mut [Route; MAX_ROUTE] {
    unsafe { &mut *ROUTES.0.get() }
}
unsafe fn conns() -> &'static mut [Conn; MAX_CONN] {
    unsafe { &mut *CONNS.0.get() }
}

fn cstr_copy(dst: &mut [u8], src: *const u8) -> bool {
    if src.is_null() {
        return false;
    }
    dst.fill(0);
    let mut i = 0usize;
    unsafe {
        while i + 1 < dst.len() {
            let b = *src.add(i);
            if b == 0 {
                break;
            }
            dst[i] = b;
            i += 1;
        }
    }
    true
}

fn cstr_eq(stored: &[u8], got: &[u8]) -> bool {
    let mut n = 0usize;
    while n < stored.len() && stored[n] != 0 {
        n += 1;
    }
    n == got.len() && stored[..n] == got[..]
}

fn find_headers_end(buf: &[u8]) -> Option<usize> {
    buf.windows(4).position(|w| w == b"\r\n\r\n").map(|i| i + 4)
}

fn parse_req<'a>(buf: &'a [u8]) -> Option<(&'a [u8], &'a [u8])> {
    let line_end = buf.windows(2).position(|w| w == b"\r\n")?;
    let line = &buf[..line_end];
    let mut it = line.split(|b| *b == b' ');
    let method = it.next()?;
    let path = it.next()?;
    Some((method, path))
}

fn lookup(method: &[u8], path: &[u8]) -> (*const u8, u32) {
    unsafe {
        for r in routes().iter() {
            if r.used && cstr_eq(&r.method, method) && cstr_eq(&r.path, path) {
                return (r.body.as_ptr(), r.body_len);
            }
        }
    }
    (DEFAULT_BODY.as_ptr(), DEFAULT_BODY.len() as u32)
}

fn conn_slot() -> Option<usize> {
    unsafe { conns().iter().position(|c| !c.used) }
}

fn build_hdr(c: &mut Conn) {
    c.hdr.fill(0);
    let mut n = 0usize;
    let prefix = b"HTTP/1.0 200 OK\r\nContent-Length: ";
    c.hdr[n..n + prefix.len()].copy_from_slice(prefix);
    n += prefix.len();
    let mut tmp = [0u8; 10];
    let mut v = c.body_len;
    if v == 0 {
        c.hdr[n] = b'0';
        n += 1;
    } else {
        let mut i = 0usize;
        while v != 0 && i < tmp.len() {
            tmp[i] = b'0' + (v % 10) as u8;
            v /= 10;
            i += 1;
        }
        while i > 0 {
            i -= 1;
            c.hdr[n] = tmp[i];
            n += 1;
        }
    }
    let tail = b"\r\n\r\n";
    c.hdr[n..n + tail.len()].copy_from_slice(tail);
    n += tail.len();
    c.hdr_len = n as u32;
}

/// Must match `struct pm_metal_async_coro` in async/__types__.h.
#[repr(C)]
struct CoroHead {
    step: *mut c_void,
    awaiting: *mut c_void,
    waiter: *mut c_void,
    task: *mut c_void,
    status: u32,
}

/// Conn frame: C coro header then our slot index.
#[repr(C)]
struct ConnFrame {
    _coro: CoroHead,
    slot: u32,
}

unsafe extern "C" fn step_conn_frame(self_: *mut pm_metal_async_coro_t) -> i32 {
    let f = self_ as *mut ConnFrame;
    let slot = unsafe { (*f).slot as usize };
    if slot >= MAX_CONN {
        return ERROR;
    }
    let c = unsafe { &mut conns()[slot] };
    if c.step == 0 {
        let room = RX_MAX as u32 - c.rx_len;
        if room == 0 {
            unsafe { pm_metal_net_ip_close(c.fd) };
            c.used = false;
            return ERROR;
        }
        let n = unsafe { pm_metal_net_ip_recv(c.fd, c.rx.as_mut_ptr().add(c.rx_len as usize), room) };
        if n == 0 {
            return WAITING;
        }
        if n < 0 {
            unsafe { pm_metal_net_ip_close(c.fd) };
            c.used = false;
            return ERROR;
        }
        c.rx_len += n as u32;
        let end = match find_headers_end(&c.rx[..c.rx_len as usize]) {
            Some(e) => e,
            None => return WAITING,
        };
        let (method, path) = match parse_req(&c.rx[..end]) {
            Some(v) => v,
            None => {
                unsafe { pm_metal_net_ip_close(c.fd) };
                c.used = false;
                return ERROR;
            }
        };
        let (body, blen) = lookup(method, path);
        c.body = body;
        c.body_len = blen;
        build_hdr(c);
        c.snd_off = 0;
        c.step = 1;
    }
    if c.step == 1 {
        let off = c.snd_off as usize;
        let left = c.hdr_len as usize - off;
        let n = unsafe { pm_metal_net_ip_send(c.fd, c.hdr.as_ptr().add(off), left as u32) };
        if n == 0 {
            return WAITING;
        }
        if n < 0 {
            unsafe { pm_metal_net_ip_close(c.fd) };
            c.used = false;
            return ERROR;
        }
        c.snd_off += n as u32;
        if c.snd_off < c.hdr_len {
            return WAITING;
        }
        c.snd_off = 0;
        c.step = 2;
    }
    if c.step == 2 {
        let off = c.snd_off as usize;
        let left = c.body_len as usize - off;
        if left == 0 {
            c.step = 3;
        } else {
            let n = unsafe { pm_metal_net_ip_send(c.fd, c.body.add(off), left as u32) };
            if n == 0 {
                return WAITING;
            }
            if n < 0 {
                unsafe { pm_metal_net_ip_close(c.fd) };
                c.used = false;
                return ERROR;
            }
            c.snd_off += n as u32;
            if c.snd_off < c.body_len {
                return WAITING;
            }
            c.step = 3;
        }
    }
    unsafe { pm_metal_net_ip_close(c.fd) };
    c.used = false;
    DONE
}

fn spawn_conn(fd: i32) -> i32 {
    let Some(slot) = conn_slot() else {
        unsafe { pm_metal_net_ip_close(fd) };
        return -1;
    };
    unsafe {
        let c = &mut conns()[slot];
        *c = Conn {
            used: true,
            step: 0,
            fd,
            rx: [0; RX_MAX],
            rx_len: 0,
            snd_off: 0,
            hdr_len: 0,
            hdr: [0; HDR_MAX],
            body: ptr::null(),
            body_len: 0,
        };
        let coro = pm_metal_async_coro_create(step_conn_frame, core::mem::size_of::<ConnFrame>());
        if coro.is_null() {
            c.used = false;
            pm_metal_net_ip_close(fd);
            return -1;
        }
        (*(coro as *mut ConnFrame)).slot = slot as u32;
        if pm_metal_async_create_task(coro).is_null() {
            c.used = false;
            pm_metal_net_ip_close(fd);
            return -1;
        }
    }
    0
}

#[repr(C)]
struct ListenFrame {
    _coro: CoroHead,
}

unsafe extern "C" fn step_listen(_self: *mut pm_metal_async_coro_t) -> i32 {
    let fd = unsafe { *LISTEN_FD.0.get() };
    if fd < 0 {
        return ERROR;
    }
    loop {
        let a = unsafe { pm_metal_net_ip_accept(fd) };
        if a == ACCEPT_WAIT {
            return WAITING;
        }
        if a < 0 {
            return ERROR;
        }
        if spawn_conn(a) != 0 {
            return ERROR;
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_init(arena: *mut pm_util_mem_arena_t) -> i32 {
    if arena.is_null() {
        return -1;
    }
    unsafe {
        *ARENA.0.get() = arena;
        *LISTEN_FD.0.get() = -1;
        for r in routes().iter_mut() {
            r.used = false;
        }
        for c in conns().iter_mut() {
            c.used = false;
        }
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_deinit() {
    unsafe {
        let fd = *LISTEN_FD.0.get();
        if fd >= 0 {
            pm_metal_net_ip_close(fd);
            *LISTEN_FD.0.get() = -1;
        }
        *ARENA.0.get() = ptr::null_mut();
        for c in conns().iter_mut() {
            if c.used && c.fd >= 0 {
                pm_metal_net_ip_close(c.fd);
            }
            c.used = false;
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route(
    method: *const u8,
    path: *const u8,
    body: *const u8,
    body_len: u32,
) -> i32 {
    if method.is_null() || path.is_null() {
        return -1;
    }
    if body_len as usize > 256 {
        return -1;
    }
    unsafe {
        let Some(slot) = routes().iter().position(|r| !r.used) else {
            return -1;
        };
        let r = &mut routes()[slot];
        if !cstr_copy(&mut r.method, method) || !cstr_copy(&mut r.path, path) {
            return -1;
        }
        r.body.fill(0);
        if body_len != 0 && !body.is_null() {
            ptr::copy_nonoverlapping(body, r.body.as_mut_ptr(), body_len as usize);
        }
        r.body_len = body_len;
        r.used = true;
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_listen(addr: u32, port: u16) -> i32 {
    unsafe {
        if (*ARENA.0.get()).is_null() {
            return -1;
        }
        if *LISTEN_FD.0.get() >= 0 {
            return 0;
        }
        let fd = pm_metal_net_ip_socket(SOCK_STREAM);
        if fd < 0 || pm_metal_net_ip_bind(fd, addr, port) != 0 || pm_metal_net_ip_listen(fd, 4) != 0
        {
            if fd >= 0 {
                pm_metal_net_ip_close(fd);
            }
            return -1;
        }
        *LISTEN_FD.0.get() = fd;
        let coro = pm_metal_async_coro_create(step_listen, core::mem::size_of::<ListenFrame>());
        if coro.is_null() || pm_metal_async_create_task(coro).is_null() {
            pm_metal_net_ip_close(fd);
            *LISTEN_FD.0.get() = -1;
            return -1;
        }
    }
    0
}

pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_init,
    "int32_t(pm_util_mem_arena_t *)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_deinit,
    "void(void)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route,
    "int32_t(const char *, const char *, const uint8_t *, uint32_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_listen,
    "int32_t(uint32_t, uint16_t)"
);
pymergetic_wasmmod::PM_MOD_BOOT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_init,
    pm_metal_net_http_asgi_deinit
);
pymergetic_wasmmod::PM_MOD_BOOTDEP_RS!(
    "pymergetic.metal.net.http.asgi",
    "pymergetic.metal.net.http"
);
