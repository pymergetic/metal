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
const MAX_ROUTE: usize = 24;
const MAX_CONN: usize = 4;
const RX_MAX: usize = 1024;
const HDR_MAX: usize = 160;
const BODY_MAX: usize = 16384;

type Handler = unsafe extern "C" fn(
    method: *const u8,
    path: *const u8,
    out: *mut u8,
    out_max: u32,
    out_len: *mut u32,
) -> i32;

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
    handler: Option<Handler>,
    /* Caller-owned bytes for route_static. Null means use `body`. */
    ext: *const u8,
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
    body_buf: [u8; BODY_MAX],
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
    handler: None,
    ext: ptr::null(),
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
    body_buf: [0; BODY_MAX],
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

fn path_only(path: &[u8]) -> &[u8] {
    match path.iter().position(|&b| b == b'?') {
        Some(i) => &path[..i],
        None => path,
    }
}

fn copy_cstr(dst: &mut [u8], src: &[u8]) {
    dst.fill(0);
    let n = src.len().min(dst.len().saturating_sub(1));
    dst[..n].copy_from_slice(&src[..n]);
}

fn lookup_into(c: &mut Conn, method: &[u8], path: &[u8]) {
    let match_path = path_only(path);
    unsafe {
        for r in routes().iter() {
            if !(r.used && cstr_eq(&r.method, method) && cstr_eq(&r.path, match_path)) {
                continue;
            }
            if let Some(h) = r.handler {
                let mut mbuf = [0u8; 8];
                let mut pbuf = [0u8; 160];
                copy_cstr(&mut mbuf, method);
                copy_cstr(&mut pbuf, path);
                let mut n = 0u32;
                let rc = h(
                    mbuf.as_ptr(),
                    pbuf.as_ptr(),
                    c.body_buf.as_mut_ptr(),
                    BODY_MAX as u32,
                    &mut n,
                );
                if rc == 0 && n as usize <= BODY_MAX {
                    c.body = c.body_buf.as_ptr();
                    c.body_len = n;
                    return;
                }
            } else if !r.ext.is_null() {
                c.body = r.ext;
                c.body_len = r.body_len;
                return;
            } else {
                c.body = r.body.as_ptr();
                c.body_len = r.body_len;
                return;
            }
        }
    }
    c.body = DEFAULT_BODY.as_ptr();
    c.body_len = DEFAULT_BODY.len() as u32;
}

fn conn_slot() -> Option<usize> {
    unsafe { conns().iter().position(|c| !c.used) }
}

fn ctype_of(path: &[u8]) -> &'static [u8] {
    let mut end = path.len();
    while end > 0 && path[end - 1] == 0 {
        end -= 1;
    }
    let p = &path[..end];
    let dot = p.iter().rposition(|&b| b == b'.');
    match dot.map(|i| &p[i..]) {
        Some(b".html") | Some(b".htm") => b"text/html; charset=utf-8",
        Some(b".css") => b"text/css; charset=utf-8",
        Some(b".js") => b"application/javascript",
        Some(b".json") => b"application/json",
        Some(b".svg") => b"image/svg+xml",
        Some(b".png") => b"image/png",
        _ => b"application/octet-stream",
    }
}

fn build_hdr(c: &mut Conn, path: &[u8]) {
    c.hdr.fill(0);
    let mut n = 0usize;
    let status = b"HTTP/1.0 200 OK\r\nContent-Type: ";
    c.hdr[n..n + status.len()].copy_from_slice(status);
    n += status.len();
    let ct = ctype_of(path);
    c.hdr[n..n + ct.len()].copy_from_slice(ct);
    n += ct.len();
    let prefix = b"\r\nContent-Length: ";
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
        let mut mbuf = [0u8; 8];
        let mut pbuf = [0u8; 160];
        let mlen = method.len().min(7);
        let plen = path.len().min(159);
        copy_cstr(&mut mbuf, method);
        copy_cstr(&mut pbuf, path);
        lookup_into(c, &mbuf[..mlen], &pbuf[..plen]);
        build_hdr(c, &pbuf[..plen]);
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
            body_buf: [0; BODY_MAX],
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
        r.handler = None;
        r.ext = ptr::null();
        r.used = true;
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_fn(
    method: *const u8,
    path: *const u8,
    handler: Option<Handler>,
) -> i32 {
    if method.is_null() || path.is_null() || handler.is_none() {
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
        r.body_len = 0;
        r.handler = handler;
        r.ext = ptr::null();
        r.used = true;
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_static(
    url: *const u8,
    body: *const u8,
    body_len: u32,
) -> i32 {
    if url.is_null() || (body_len != 0 && body.is_null()) {
        return -1;
    }
    unsafe {
        let Some(slot) = routes().iter().position(|r| !r.used) else {
            return -1;
        };
        let r = &mut routes()[slot];
        if !cstr_copy(&mut r.method, b"GET\0".as_ptr()) || !cstr_copy(&mut r.path, url) {
            return -1;
        }
        r.body.fill(0);
        r.body_len = body_len;
        r.handler = None;
        r.ext = body;
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
    pm_metal_net_http_asgi_route_fn,
    "int32_t(const char *, const char *, pm_metal_net_http_asgi_handler_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route_static,
    "int32_t(const char *, const uint8_t *, uint32_t)"
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
