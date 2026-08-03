//! HTTP/1.1 server (ASGI C mounts) on the opaque TCP stream — C mounts only (v1).
//!
//! One connection at a time per listener coro; `Connection: close` after each
//! response. Cleartext or TLS via `tcp_accept(..., creds_h)`.

use core::ffi::{c_char, c_void};
use core::ptr::{self, addr_of};

use pymergetic_metal_async as _;
use pymergetic_metal_net_ip as ip;
use pymergetic_metal_net_ip::coro::{self, Child};
use pymergetic_metal_net_ip_tcp as tcp;
use pymergetic_metal_rt as _;

const APP_SLOTS: usize = 32;
const SRV_MAX: usize = 4;
const MOUNT_MAX: usize = 32;
const PATH_MAX: usize = 256;
const HDR_MAX: usize = 8192;
const RESP_HDR_MAX: usize = 512;
const IO_CHUNK: usize = 1024;
const IO_US: u64 = 30_000_000;

const PHASE_ACCEPT: u32 = 0;
const PHASE_ACCEPT_WAIT: u32 = 1;
const PHASE_RECV: u32 = 2;
const PHASE_RECV_WAIT: u32 = 3;

pub type pm_metal_net_http_c_fn =
    Option<unsafe extern "C" fn(ctx: *mut c_void, conn_id: u32) -> i32>;

#[derive(Copy, Clone)]
struct AppSlot {
    used: bool,
    func: pm_metal_net_http_c_fn,
    ctx: *mut c_void,
}

#[derive(Copy, Clone)]
struct Mount {
    used: bool,
    path: [u8; PATH_MAX],
    app_h: u32,
}

#[derive(Copy, Clone)]
struct Server {
    used: bool,
    port: u16,
    creds_h: u32,
    listen_h: u32,
    coro_h: u32,
    mounts: [Mount; MOUNT_MAX],
}

#[repr(C)]
struct ListenFrame {
    phase: u32,
    srv_h: u32,
    listen_h: u32,
    creds_h: u32,
    child_h: u32,
    conn_h: u32,
    hdr: [u8; HDR_MAX],
    hdr_len: u32,
    deadline: u64,
    chunk: [u8; IO_CHUNK],
}

static mut APPS: [AppSlot; APP_SLOTS] = [AppSlot {
    used: false,
    func: None as pm_metal_net_http_c_fn,
    ctx: ptr::null_mut(),
}; APP_SLOTS];
static mut SERVERS: [Server; SRV_MAX] = [Server {
    used: false,
    port: 0,
    creds_h: 0,
    listen_h: 0,
    coro_h: 0,
    mounts: [Mount {
        used: false,
        path: [0; PATH_MAX],
        app_h: 0,
    }; MOUNT_MAX],
}; SRV_MAX];
static mut CURRENT_CONN: u32 = 0;
static mut HEALTH_APP: u32 = 0;
static mut AUTO_SRV: u32 = 0;
static mut CONN_METHOD: [u8; 16] = [0; 16];
static mut CONN_TARGET: [u8; PATH_MAX] = [0; PATH_MAX];
static mut CONN_HDR: [u8; HDR_MAX] = [0; HDR_MAX];
static mut CONN_HDR_LEN: u32 = 0;

extern "C" {
    fn pm_metal_async_create_task(h: u32) -> u32;
}

unsafe fn app_slot(h: u32) -> *mut AppSlot {
    if h == 0 || (h as usize) > APP_SLOTS {
        return ptr::null_mut();
    }
    let s = &mut APPS[(h as usize) - 1];
    if !s.used {
        return ptr::null_mut();
    }
    s
}

unsafe fn srv_slot(h: u32) -> *mut Server {
    if h == 0 || (h as usize) > SRV_MAX {
        return ptr::null_mut();
    }
    let s = &mut SERVERS[(h as usize) - 1];
    if !s.used {
        return ptr::null_mut();
    }
    s
}

unsafe fn app_alloc() -> u32 {
    for i in 0..APP_SLOTS {
        if !APPS[i].used {
            APPS[i] = AppSlot {
                used: true,
                func: None,
                ctx: ptr::null_mut(),
            };
            return (i + 1) as u32;
        }
    }
    0
}

unsafe fn srv_alloc() -> u32 {
    for i in 0..SRV_MAX {
        if !SERVERS[i].used {
            SERVERS[i] = Server {
                used: true,
                port: 0,
                creds_h: 0,
                listen_h: 0,
                coro_h: 0,
                mounts: [Mount {
                    used: false,
                    path: [0; PATH_MAX],
                    app_h: 0,
                }; MOUNT_MAX],
            };
            return (i + 1) as u32;
        }
    }
    0
}

unsafe fn cstr_len(s: *const c_char, max: usize) -> usize {
    coro::cstr_len(s, max)
}

unsafe fn path_eq(stored: &[u8], src: *const c_char) -> bool {
    if src.is_null() {
        return false;
    }
    let slen = stored.iter().position(|&b| b == 0).unwrap_or(PATH_MAX);
    let n = cstr_len(src, PATH_MAX);
    if slen != n {
        return false;
    }
    &stored[..n] == unsafe { core::slice::from_raw_parts(src as *const u8, n) }
}

unsafe fn copy_path(dst: &mut [u8], src: *const c_char) -> bool {
    coro::copy_cstr(dst, src)
}

unsafe fn mount_find(srv: *mut Server, target: &[u8]) -> *mut Mount {
    let mut best: *mut Mount = ptr::null_mut();
    let mut best_len = 0usize;
    for i in 0..MOUNT_MAX {
        let m = &mut (*srv).mounts[i];
        if !m.used {
            continue;
        }
        let plen = m.path.iter().position(|&b| b == 0).unwrap_or(PATH_MAX);
        if plen == 0 {
            continue;
        }
        let path = &m.path[..plen];
        if path == b"/" {
            if best.is_null() {
                best = m;
                best_len = 1;
            }
            continue;
        }
        if target.len() >= plen
            && target[..plen] == path[..]
            && (target.len() == plen || target[plen] == b'/')
            && plen >= best_len
        {
            best = m;
            best_len = plen;
        }
    }
    best
}

fn headers_done(hdr: &[u8]) -> bool {
    hdr.len() >= 4 && hdr.ends_with(b"\r\n\r\n")
}

unsafe fn parse_request_line(hdr: &[u8], method: &mut [u8], target: &mut [u8]) -> bool {
    let end = hdr.iter().position(|&b| b == b'\r').unwrap_or(hdr.len());
    let line = &hdr[..end];
    let sp1 = line.iter().position(|&b| b == b' ');
    let Some(sp1) = sp1 else {
        return false;
    };
    let sp2 = line[sp1 + 1..].iter().position(|&b| b == b' ');
    let Some(sp2) = sp2 else {
        return false;
    };
    let sp2 = sp1 + 1 + sp2;
    let meth = &line[..sp1];
    let req_target = &line[sp1 + 1..sp2];
    if meth.is_empty() || req_target.is_empty() {
        return false;
    }
    if meth.len() >= method.len() || req_target.len() >= target.len() {
        return false;
    }
    method[..meth.len()].copy_from_slice(meth);
    method[meth.len()] = 0;
    /* Keep query string in conn_target so apps can read ?path=...;
     * mount_find strips it separately. */
    if req_target.is_empty() {
        return false;
    }
    target[..req_target.len()].copy_from_slice(req_target);
    target[req_target.len()] = 0;
    true
}

struct Writer<'a> {
    buf: &'a mut [u8],
    len: usize,
    ok: bool,
}

impl<'a> Writer<'a> {
    fn new(buf: &'a mut [u8]) -> Self {
        Self {
            buf,
            len: 0,
            ok: true,
        }
    }
    fn bytes(&mut self, src: &[u8]) {
        if !self.ok || self.len + src.len() > self.buf.len() {
            self.ok = false;
            return;
        }
        self.buf[self.len..self.len + src.len()].copy_from_slice(src);
        self.len += src.len();
    }
    fn cstr(&mut self, src: *const c_char) {
        if src.is_null() {
            self.ok = false;
            return;
        }
        let n = unsafe { cstr_len(src, self.buf.len()) };
        self.bytes(unsafe { core::slice::from_raw_parts(src as *const u8, n) });
    }
    fn dec(&mut self, mut v: u32) {
        let mut digits = [0u8; 10];
        let mut n = 0;
        loop {
            digits[n] = b'0' + (v % 10) as u8;
            n += 1;
            v /= 10;
            if v == 0 {
                break;
            }
        }
        for i in (0..n).rev() {
            self.bytes(&digits[i..i + 1]);
        }
    }
    fn finish(self) -> Option<usize> {
        if self.ok {
            Some(self.len)
        } else {
            None
        }
    }
}

unsafe fn sync_tcp_write(conn_h: u32, buf: *const u8, len: u32) -> bool {
    if conn_h == 0 || buf.is_null() || len == 0 {
        return false;
    }
    let mut off = 0u32;
    let mut deadline = coro::pm_metal_time_mono_us() + IO_US;
    while off < len {
        /* try_write: no nested async poll (listen handler may already be in poll_all). */
        let n = tcp::pm_metal_net_ip_tcp_try_write(conn_h, buf.add(off as usize), len - off);
        if n > 0 {
            off += n;
            deadline = coro::pm_metal_time_mono_us() + IO_US;
            continue;
        }
        if coro::pm_metal_time_mono_us() >= deadline {
            return false;
        }
        ip::pm_metal_net_ip_poll();
    }
    true
}

unsafe fn build_simple_response(
    code: u32,
    reason: *const c_char,
    ctype: *const c_char,
    blen: u32,
    out: &mut [u8],
) -> Option<usize> {
    let mut w = Writer::new(out);
    w.bytes(b"HTTP/1.1 ");
    w.dec(code);
    w.bytes(b" ");
    w.cstr(reason);
    w.bytes(b"\r\nServer: metal-http\r\n");
    if !ctype.is_null() {
        w.bytes(b"Content-Type: ");
        w.cstr(ctype);
        w.bytes(b"\r\n");
    }
    w.bytes(b"Content-Length: ");
    w.dec(blen);
    w.bytes(b"\r\nConnection: close\r\n\r\n");
    w.finish()
}

unsafe fn send_bytes_on_conn(
    conn_h: u32,
    code: u32,
    reason: *const c_char,
    ctype: *const c_char,
    body: *const u8,
    blen: u32,
) -> i32 {
    if conn_h == 0 {
        return -1;
    }
    let mut hdr = [0u8; RESP_HDR_MAX];
    let Some(hlen) = build_simple_response(code, reason, ctype, blen, &mut hdr) else {
        return -1;
    };
    if !sync_tcp_write(conn_h, hdr.as_ptr(), hlen as u32) {
        return -1;
    }
    if blen > 0 && !body.is_null() && !sync_tcp_write(conn_h, body, blen) {
        return -1;
    }
    0
}

unsafe fn send_simple_on_conn(
    conn_h: u32,
    code: u32,
    reason: *const c_char,
    ctype: *const c_char,
    body: *const c_char,
) -> i32 {
    let blen = if body.is_null() {
        0
    } else {
        cstr_len(body, 1_048_576) as u32
    };
    send_bytes_on_conn(conn_h, code, reason, ctype, body as *const u8, blen)
}

unsafe extern "C" fn health_app_fn(_ctx: *mut c_void, _conn_id: u32) -> i32 {
    pm_metal_net_http_send_simple(
        200,
        b"OK\0".as_ptr() as *const c_char,
        b"text/plain\0".as_ptr() as *const c_char,
        b"ok\n\0".as_ptr() as *const c_char,
    )
}

unsafe fn conn_clear() {
    CURRENT_CONN = 0;
    CONN_METHOD = [0; 16];
    CONN_TARGET = [0; PATH_MAX];
    CONN_HDR = [0; HDR_MAX];
    CONN_HDR_LEN = 0;
}

unsafe fn conn_begin(conn_h: u32, hdr: &[u8], method: &[u8], target: &[u8]) {
    CURRENT_CONN = conn_h;
    CONN_METHOD = [0; 16];
    CONN_TARGET = [0; PATH_MAX];
    CONN_HDR = [0; HDR_MAX];
    let mlen = method
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(method.len())
        .min(15);
    CONN_METHOD[..mlen].copy_from_slice(&method[..mlen]);
    let tlen = target
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(target.len())
        .min(PATH_MAX - 1);
    CONN_TARGET[..tlen].copy_from_slice(&target[..tlen]);
    let hlen = hdr.len().min(HDR_MAX - 1);
    CONN_HDR[..hlen].copy_from_slice(&hdr[..hlen]);
    CONN_HDR_LEN = hlen as u32;
}

unsafe fn handle_conn(srv_h: u32, conn_h: u32, hdr: &[u8]) {
    let mut method = [0u8; 16];
    let mut target = [0u8; PATH_MAX];
    if !parse_request_line(hdr, &mut method, &mut target) {
        CURRENT_CONN = conn_h;
        let _ = send_simple_on_conn(
            conn_h,
            400,
            b"Bad Request\0".as_ptr() as *const c_char,
            b"text/plain\0".as_ptr() as *const c_char,
            b"bad request\n\0".as_ptr() as *const c_char,
        );
        conn_clear();
        return;
    }
    conn_begin(conn_h, hdr, &method, &target);
    let tlen = target.iter().position(|&b| b == 0).unwrap_or(PATH_MAX);
    let target_full = &target[..tlen];
    let path_end = target_full
        .iter()
        .position(|&b| b == b'?')
        .unwrap_or(tlen);
    let target_path = &target_full[..path_end];
    let srv = srv_slot(srv_h);
    if srv.is_null() {
        conn_clear();
        return;
    }
    let m = mount_find(srv, target_path);
    if m.is_null() {
        let _ = send_simple_on_conn(
            conn_h,
            404,
            b"Not Found\0".as_ptr() as *const c_char,
            b"text/plain\0".as_ptr() as *const c_char,
            b"no routes\n\0".as_ptr() as *const c_char,
        );
        conn_clear();
        return;
    }
    let app_h = (*m).app_h;
    let slot = app_slot(app_h);
    if slot.is_null() {
        let _ = send_simple_on_conn(
            conn_h,
            500,
            b"Error\0".as_ptr() as *const c_char,
            b"text/plain\0".as_ptr() as *const c_char,
            b"bad app\n\0".as_ptr() as *const c_char,
        );
        conn_clear();
        return;
    }
    let Some(func) = (*slot).func else {
        let _ = send_simple_on_conn(
            conn_h,
            500,
            b"Error\0".as_ptr() as *const c_char,
            b"text/plain\0".as_ptr() as *const c_char,
            b"bad app\n\0".as_ptr() as *const c_char,
        );
        conn_clear();
        return;
    };
    let rc = func((*slot).ctx, conn_h);
    if rc != 0 {
        let _ = send_simple_on_conn(
            conn_h,
            500,
            b"Error\0".as_ptr() as *const c_char,
            b"text/plain\0".as_ptr() as *const c_char,
            b"app fail\n\0".as_ptr() as *const c_char,
        );
    }
    conn_clear();
}

unsafe extern "C" fn listen_step(self_h: u32) -> u32 {
    let f = coro::frame::<ListenFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }
    let srv = srv_slot((*f).srv_h);
    if srv.is_null() {
        return coro::ERROR;
    }
    match (*f).phase {
        PHASE_ACCEPT => {
            (*f).child_h = tcp::pm_metal_net_ip_tcp_accept((*f).listen_h, (*f).creds_h);
            if (*f).child_h == 0 {
                return coro::ERROR;
            }
            (*f).phase = PHASE_ACCEPT_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        PHASE_ACCEPT_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => {
                (*f).phase = PHASE_ACCEPT;
                coro::PENDING
            }
            Child::Done(ch) => {
                if ch == 0 {
                    (*f).phase = PHASE_ACCEPT;
                    return coro::PENDING;
                }
                (*f).conn_h = ch;
                (*f).hdr_len = 0;
                (*f).hdr = [0; HDR_MAX];
                (*f).phase = PHASE_RECV;
                coro::PENDING
            }
        },
        PHASE_RECV => {
            let frame = &mut *f;
            let hdr_so_far = &frame.hdr[..frame.hdr_len as usize];
            if headers_done(hdr_so_far) {
                handle_conn(frame.srv_h, frame.conn_h, hdr_so_far);
                tcp::pm_metal_net_ip_tcp_close(frame.conn_h);
                frame.conn_h = 0;
                frame.phase = PHASE_ACCEPT;
                return coro::PENDING;
            }
            if frame.hdr_len as usize >= HDR_MAX {
                handle_conn(frame.srv_h, frame.conn_h, hdr_so_far);
                tcp::pm_metal_net_ip_tcp_close(frame.conn_h);
                frame.conn_h = 0;
                frame.phase = PHASE_ACCEPT;
                return coro::PENDING;
            }
            frame.child_h = tcp::pm_metal_net_ip_tcp_read(
                frame.conn_h,
                frame.chunk.as_mut_ptr(),
                IO_CHUNK as u32,
            );
            if frame.child_h == 0 {
                tcp::pm_metal_net_ip_tcp_close(frame.conn_h);
                frame.conn_h = 0;
                frame.phase = PHASE_ACCEPT;
                return coro::PENDING;
            }
            frame.phase = PHASE_RECV_WAIT;
            coro::pm_metal_async_await(self_h, frame.child_h)
        }
        PHASE_RECV_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed | Child::Done(0) => {
                if (*f).conn_h != 0 {
                    tcp::pm_metal_net_ip_tcp_close((*f).conn_h);
                    (*f).conn_h = 0;
                }
                (*f).phase = PHASE_ACCEPT;
                coro::PENDING
            }
            Child::Done(n) => {
                let frame = &mut *f;
                let base = frame.hdr_len as usize;
                let end = base + n as usize;
                if end > HDR_MAX {
                    tcp::pm_metal_net_ip_tcp_close(frame.conn_h);
                    frame.conn_h = 0;
                    frame.phase = PHASE_ACCEPT;
                    return coro::PENDING;
                }
                frame.hdr[base..end].copy_from_slice(&frame.chunk[..n as usize]);
                frame.hdr_len += n;
                frame.phase = PHASE_RECV;
                coro::PENDING
            }
        },
        _ => coro::ERROR,
    }
}

/// Register a C leaf app. Returns app handle (0 on failure).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_register_c(
    func: pm_metal_net_http_c_fn,
    ctx: *mut c_void,
) -> u32 {
    if func.is_none() {
        return 0;
    }
    let h = app_alloc();
    if h == 0 {
        return 0;
    }
    let s = app_slot(h);
    (*s).func = func;
    (*s).ctx = ctx;
    h
}

/// Drop a registered app handle.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_unregister(app_h: u32) {
    let s = app_slot(app_h);
    if s.is_null() {
        return;
    }
    (*s).used = false;
    (*s).func = None;
    (*s).ctx = ptr::null_mut();
}

/// Listen on `port`. `creds_h == 0` cleartext; else TLS via tcp_accept.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_listen(port: u16, creds_h: u32) -> u32 {
    if port == 0 {
        return 0;
    }
    let srv_h = srv_alloc();
    if srv_h == 0 {
        return 0;
    }
    let listen_h = tcp::pm_metal_net_ip_tcp_listen(port);
    if listen_h == 0 {
        (*srv_slot(srv_h)).used = false;
        return 0;
    }
    let coro_h = coro::coro_with_frame::<ListenFrame>(listen_step);
    if coro_h == 0 {
        tcp::pm_metal_net_ip_tcp_listen_close(listen_h);
        (*srv_slot(srv_h)).used = false;
        return 0;
    }
    let f = coro::frame::<ListenFrame>(coro_h);
    (*f).phase = PHASE_ACCEPT;
    (*f).srv_h = srv_h;
    (*f).listen_h = listen_h;
    (*f).creds_h = creds_h;
    (*f).child_h = 0;
    (*f).conn_h = 0;
    (*f).hdr_len = 0;
    if pm_metal_async_create_task(coro_h) == 0 {
        coro::pm_metal_async_coro_close(coro_h);
        tcp::pm_metal_net_ip_tcp_listen_close(listen_h);
        (*srv_slot(srv_h)).used = false;
        return 0;
    }
    let srv = srv_slot(srv_h);
    (*srv).port = port;
    (*srv).creds_h = creds_h;
    (*srv).listen_h = listen_h;
    (*srv).coro_h = coro_h;
    srv_h
}

/// Mount `app_h` at `path` (longest-prefix match). Returns 0 on success.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_mount(srv_h: u32, path: *const c_char, app_h: u32) -> i32 {
    let srv = srv_slot(srv_h);
    if srv.is_null() || path.is_null() || app_h == 0 || app_slot(app_h).is_null() {
        return -1;
    }
    if *path == 0 || *path as u8 != b'/' {
        return -1;
    }
    for i in 0..MOUNT_MAX {
        let m = &mut (*srv).mounts[i];
        if m.used && path_eq(&m.path, path) {
            m.app_h = app_h;
            return 0;
        }
    }
    for i in 0..MOUNT_MAX {
        let m = &mut (*srv).mounts[i];
        if !m.used {
            if !copy_path(&mut m.path, path) {
                return -1;
            }
            m.app_h = app_h;
            m.used = true;
            return 0;
        }
    }
    -1
}

/// Remove mount at `path`. Returns 0 on success.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_unmount(srv_h: u32, path: *const c_char) -> i32 {
    let srv = srv_slot(srv_h);
    if srv.is_null() || path.is_null() {
        return -1;
    }
    for i in 0..MOUNT_MAX {
        let m = &mut (*srv).mounts[i];
        if m.used && path_eq(&m.path, path) {
            m.used = false;
            m.path = [0; PATH_MAX];
            m.app_h = 0;
            return 0;
        }
    }
    -1
}

/// Stop listener and release server slot.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_close(srv_h: u32) {
    let srv = srv_slot(srv_h);
    if srv.is_null() {
        return;
    }
    if (*srv).coro_h != 0 {
        coro::pm_metal_async_coro_close((*srv).coro_h);
        (*srv).coro_h = 0;
    }
    if (*srv).listen_h != 0 {
        tcp::pm_metal_net_ip_tcp_listen_close((*srv).listen_h);
        (*srv).listen_h = 0;
    }
    (*srv).used = false;
}

/// Reply on the active connection (set before the C app runs).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_send_simple(
    code: u32,
    reason: *const c_char,
    ctype: *const c_char,
    body: *const c_char,
) -> i32 {
    send_simple_on_conn(CURRENT_CONN, code, reason, ctype, body)
}

/// Binary-safe reply (Content-Length = `body_len`; body may contain NUL).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_send_bytes(
    code: u32,
    reason: *const c_char,
    ctype: *const c_char,
    body: *const u8,
    body_len: u32,
) -> i32 {
    send_bytes_on_conn(CURRENT_CONN, code, reason, ctype, body, body_len)
}

/// Built-in GET /health app (200 ok). Register once; returns app handle.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_health_register() -> u32 {
    if HEALTH_APP != 0 {
        return HEALTH_APP;
    }
    HEALTH_APP = pm_metal_net_http_register_c(Some(health_app_fn), ptr::null_mut());
    HEALTH_APP
}

/// Active request method (NUL-terminated), or null if no conn.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_conn_method() -> *const u8 {
    if CURRENT_CONN == 0 {
        return ptr::null();
    }
    addr_of!(CONN_METHOD).cast::<u8>()
}

/// Active request path (NUL-terminated), or null if no conn.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_conn_target() -> *const u8 {
    if CURRENT_CONN == 0 {
        return ptr::null();
    }
    addr_of!(CONN_TARGET).cast::<u8>()
}

/// Active raw request headers (may lack trailing NUL past hdr_len).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_conn_hdr() -> *const u8 {
    if CURRENT_CONN == 0 {
        return ptr::null();
    }
    addr_of!(CONN_HDR).cast::<u8>()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_conn_hdr_len() -> u32 {
    if CURRENT_CONN == 0 {
        return 0;
    }
    *addr_of!(CONN_HDR_LEN)
}

/// Write raw bytes on the active connection. Returns 0 ok, -1 fail.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_conn_send(buf: *const u8, len: u32) -> i32 {
    if CURRENT_CONN == 0 || buf.is_null() {
        return -1;
    }
    if len == 0 {
        return 0;
    }
    if sync_tcp_write(CURRENT_CONN, buf, len) {
        0
    } else {
        -1
    }
}

/// In-memory defaults: listen :80, /health + Microdot `/`. Returns 0 ok, -1 fail.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_autoload() -> i32 {
    if AUTO_SRV != 0 {
        return 0;
    }
    let health = pm_metal_net_http_health_register();
    if health == 0 {
        return -1;
    }
    let srv = pm_metal_net_http_listen(80, 0);
    if srv == 0 {
        return -1;
    }
    if pm_metal_net_http_mount(srv, b"/health\0".as_ptr() as *const c_char, health) != 0 {
        pm_metal_net_http_close(srv);
        return -1;
    }
    /* Optional Microdot root — linked by boot; skip if symbol absent is N/A. */
    extern "C" {
        fn pm_metal_net_http_microdot_register() -> u32;
    }
    let md = pm_metal_net_http_microdot_register();
    if md != 0 {
        let _ = pm_metal_net_http_mount(srv, b"/\0".as_ptr() as *const c_char, md);
    }
    AUTO_SRV = srv;
    0
}

/// Autoload listen port, or 0 if httpd is not listening.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_autoload_port() -> u16 {
    if AUTO_SRV == 0 {
        return 0;
    }
    let srv = srv_slot(AUTO_SRV);
    if srv.is_null() || !(*srv).used {
        return 0;
    }
    (*srv).port
}

/// Publish server border onto `reg` (`pymergetic.metal.net.http.server.*`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_server_bind_reg() -> i32 {
    use core::ffi::c_void;
    extern "C" {
        fn pm_metal_reg_register(module: *const u8, name: *const u8, ptr: *const c_void) -> i32;
    }
    let mod_name = b"pymergetic.metal.net.http.server\0";
    let rows: [(&[u8], *const c_void); 12] = [
        (b"register_c\0", pm_metal_net_http_register_c as *const c_void),
        (b"unregister\0", pm_metal_net_http_unregister as *const c_void),
        (b"listen\0", pm_metal_net_http_listen as *const c_void),
        (b"mount\0", pm_metal_net_http_mount as *const c_void),
        (b"unmount\0", pm_metal_net_http_unmount as *const c_void),
        (b"close\0", pm_metal_net_http_close as *const c_void),
        (b"send_simple\0", pm_metal_net_http_send_simple as *const c_void),
        (b"health_register\0", pm_metal_net_http_health_register as *const c_void),
        (b"autoload\0", pm_metal_net_http_autoload as *const c_void),
        (b"conn_method\0", pm_metal_net_http_conn_method as *const c_void),
        (b"conn_target\0", pm_metal_net_http_conn_target as *const c_void),
        (b"conn_send\0", pm_metal_net_http_conn_send as *const c_void),
    ];
    for (name, ptr) in rows {
        if pm_metal_reg_register(mod_name.as_ptr(), name.as_ptr(), ptr) != 0 {
            return -1;
        }
    }
    0
}
