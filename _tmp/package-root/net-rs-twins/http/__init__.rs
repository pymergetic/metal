//! HTTP/1.1 client GET + server mounts on the opaque TCP stream
//! (`http://` clear / `https://` SSL at connect; then read/write/close).
//!
//! Identity transfer only: a `Transfer-Encoding: chunked` response is refused
//! rather than handed back half-decoded.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

#[path = "server.rs"]
mod server;
 

use core::ffi::{c_char, c_void};
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

use pymergetic_metal_async as _;
use pymergetic_metal_net_ip::coro::{self, Child};
use pymergetic_metal_net_ip_tcp as tcp;
use pymergetic_metal_rt as _;

const HTTP_URL_MAX: usize = 384;
const HTTP_HOST_MAX: usize = 128;
const HTTP_PATH_MAX: usize = 256;
const HTTP_HEADER_MAX: usize = 4096;
const HTTP_REQUEST_MAX: usize = 512;
const HTTP_CHUNK: usize = 1024;
const HTTP_IDLE_US: u64 = 30_000_000;

const PHASE_PARSE: u32 = 0;
const PHASE_CONNECT: u32 = 1;
const PHASE_CONNECT_WAIT: u32 = 2;
const PHASE_SEND: u32 = 3;
const PHASE_SEND_WAIT: u32 = 4;
const PHASE_RECV: u32 = 5;
const PHASE_RECV_WAIT: u32 = 6;

#[repr(C)]
struct HttpFrame {
    phase: u32,
    url: [u8; HTTP_URL_MAX],
    host: [u8; HTTP_HOST_MAX],
    path: [u8; HTTP_PATH_MAX],
    port: u16,
    use_ssl: u32,
    child_h: u32,
    conn_h: u32,
    deadline: u64,
    request: [u8; HTTP_REQUEST_MAX],
    request_len: u32,
    request_off: u32,
    header: [u8; HTTP_HEADER_MAX],
    header_len: u32,
    headers_done: u32,
    status: u32,
    have_content_len: u32,
    content_len: u32,
    body_len: u32,
    complete: u32,
    remote_closed: u32,
    dest: *mut u8,
    dest_cap: u32,
    chunk: [u8; HTTP_CHUNK],
}

static LAST_VALID: AtomicBool = AtomicBool::new(false);
static LAST_STATUS: AtomicU32 = AtomicU32::new(0);
static LAST_BODY_LEN: AtomicU32 = AtomicU32::new(0);

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
    fn cstr(&mut self, src: &[u8]) {
        let n = src.iter().position(|&b| b == 0).unwrap_or(src.len());
        self.bytes(&src[..n]);
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
        if self.ok && self.len > 0 {
            Some(self.len)
        } else {
            None
        }
    }
}

fn starts_with_ci(hay: &[u8], prefix: &[u8]) -> bool {
    if hay.len() < prefix.len() {
        return false;
    }
    hay[..prefix.len()]
        .iter()
        .zip(prefix.iter())
        .all(|(a, b)| a.to_ascii_lowercase() == b.to_ascii_lowercase())
}

fn contains_ci(hay: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() || hay.len() < needle.len() {
        return false;
    }
    hay.windows(needle.len()).any(|w| {
        w.iter()
            .zip(needle.iter())
            .all(|(a, b)| a.to_ascii_lowercase() == b.to_ascii_lowercase())
    })
}

unsafe fn parse_url(f: *mut HttpFrame) -> bool {
    let frame = &mut *f;
    let n = frame.url.iter().position(|&b| b == 0).unwrap_or(frame.url.len());
    let url = &frame.url[..n];
    let (rest, default_port, ssl) = if let Some(r) = url.strip_prefix(b"https://") {
        (r, 443u16, 1u32)
    } else if let Some(r) = url.strip_prefix(b"http://") {
        (r, 80u16, 0u32)
    } else {
        return false;
    };
    frame.use_ssl = ssl;

    let host_len = rest
        .iter()
        .position(|&b| b == b':' || b == b'/')
        .unwrap_or(rest.len());
    let host = &rest[..host_len];
    if host.is_empty() || host_len >= HTTP_HOST_MAX || host.iter().any(|&b| b == b'[' || b == b']') {
        return false;
    }
    frame.host[..host_len].copy_from_slice(host);
    frame.host[host_len] = 0;
    frame.port = default_port;

    let mut tail = &rest[host_len..];
    if let Some(after_colon) = tail.strip_prefix(b":") {
        let digits = after_colon
            .iter()
            .position(|&b| !b.is_ascii_digit())
            .unwrap_or(after_colon.len());
        if digits == 0 {
            return false;
        }
        let mut port: u32 = 0;
        for &b in &after_colon[..digits] {
            port = port * 10 + (b - b'0') as u32;
            if port > 65535 {
                return false;
            }
        }
        tail = &after_colon[digits..];
        if port == 0 || !(tail.is_empty() || tail[0] == b'/') {
            return false;
        }
        frame.port = port as u16;
    }

    let path: &[u8] = if tail.is_empty() { &b"/"[..] } else { tail };
    if path.len() >= HTTP_PATH_MAX {
        return false;
    }
    frame.path[..path.len()].copy_from_slice(path);
    frame.path[path.len()] = 0;
    true
}

unsafe fn parse_headers(f: *mut HttpFrame) -> bool {
    let frame = &mut *f;
    let len = frame.header_len as usize;
    let header = &frame.header[..len];
    if len < 12 || !header.starts_with(b"HTTP/1.") {
        return false;
    }
    let Some(sp) = header[7..].iter().position(|&b| b == b' ') else {
        return false;
    };
    let code = 7 + sp + 1;
    if code + 3 > len || !header[code..code + 3].iter().all(|b| b.is_ascii_digit()) {
        return false;
    }
    let mut status = 0u32;
    for &b in &header[code..code + 3] {
        status = status * 10 + (b - b'0') as u32;
    }

    let mut have_content_len = false;
    let mut content_len = 0u32;
    let mut line_start = 0;
    while line_start + 1 < len {
        let Some(crlf) = header[line_start..]
            .windows(2)
            .position(|w| w == b"\r\n")
            .map(|at| line_start + at)
        else {
            break;
        };
        let line = &header[line_start..crlf];
        if line.is_empty() {
            break;
        }
        if starts_with_ci(line, b"Content-Length:") {
            let value = line[b"Content-Length:".len()..]
                .iter()
                .copied()
                .skip_while(|b| *b == b' ' || *b == b'\t');
            let mut n = 0u32;
            let mut digits = 0;
            for b in value {
                if !b.is_ascii_digit() {
                    break;
                }
                let digit = (b - b'0') as u32;
                if n > (u32::MAX - digit) / 10 {
                    return false;
                }
                n = n * 10 + digit;
                digits += 1;
            }
            if digits == 0 {
                return false;
            }
            content_len = n;
            have_content_len = true;
        }
        if starts_with_ci(line, b"Transfer-Encoding:") && contains_ci(line, b"chunked") {
            return false;
        }
        line_start = crlf + 2;
    }

    frame.status = status;
    frame.have_content_len = have_content_len as u32;
    frame.content_len = content_len;
    if have_content_len && content_len == 0 {
        frame.complete = 1;
    }
    true
}

fn body_wanted(frame: &HttpFrame) -> u32 {
    let wanted = if frame.have_content_len != 0 {
        frame.content_len
    } else {
        frame.dest_cap
    };
    wanted.min(frame.dest_cap)
}

unsafe fn feed(f: *mut HttpFrame, data: *const u8, len: u32) {
    for i in 0..len as usize {
        let frame = &mut *f;
        if frame.complete != 0 {
            return;
        }
        let byte = *data.add(i);
        if frame.headers_done != 0 {
            let wanted = body_wanted(frame);
            if frame.body_len < wanted {
                *frame.dest.add(frame.body_len as usize) = byte;
                frame.body_len += 1;
            }
            if frame.body_len >= wanted {
                frame.complete = 1;
            }
            continue;
        }
        let at = frame.header_len as usize;
        if at + 1 >= HTTP_HEADER_MAX {
            return;
        }
        frame.header[at] = byte;
        frame.header[at + 1] = 0;
        frame.header_len += 1;
        let now = frame.header_len as usize;
        if now >= 4 && frame.header[now - 4..now] == *b"\r\n\r\n" {
            frame.headers_done = 1;
            if !parse_headers(f) {
                (*f).complete = 0;
                (*f).headers_done = 0;
                (*f).header_len = HTTP_HEADER_MAX as u32;
            }
        }
    }
}

unsafe fn finish(f: *mut HttpFrame, self_h: u32, ok: bool) -> u32 {
    if (*f).conn_h != 0 {
        tcp::pm_metal_net_ip_tcp_close((*f).conn_h);
        (*f).conn_h = 0;
    }
    if (*f).child_h != 0 {
        coro::pm_metal_async_coro_close((*f).child_h);
        (*f).child_h = 0;
    }
    LAST_VALID.store(true, Ordering::Relaxed);
    LAST_STATUS.store(if ok { (*f).status } else { 0 }, Ordering::Relaxed);
    LAST_BODY_LEN.store((*f).body_len, Ordering::Relaxed);
    coro::pm_metal_async_set_result_u32(self_h, if ok { (*f).body_len } else { 0 });
    if ok {
        coro::DONE
    } else {
        coro::ERROR
    }
}

unsafe fn build_request(f: *mut HttpFrame) -> bool {
    if (*f).request_len != 0 {
        return true;
    }
    let default_port = if (*f).use_ssl != 0 { 443 } else { 80 };
    let mut w = Writer::new(&mut (*f).request);
    w.bytes(b"GET ");
    w.cstr(&(*f).path);
    w.bytes(b" HTTP/1.1\r\nHost: ");
    w.cstr(&(*f).host);
    if (*f).port != default_port {
        w.bytes(b":");
        w.dec((*f).port as u32);
    }
    w.bytes(b"\r\nConnection: close\r\n\r\n");
    match w.finish() {
        Some(len) => {
            (*f).request_len = len as u32;
            true
        }
        None => false,
    }
}

unsafe extern "C" fn http_step(self_h: u32) -> u32 {
    let f = coro::frame::<HttpFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }

    match (*f).phase {
        PHASE_PARSE => {
            if !parse_url(f) {
                return finish(f, self_h, false);
            }
            (*f).phase = PHASE_CONNECT;
            coro::PENDING
        }
        PHASE_CONNECT => {
            let opts = if (*f).use_ssl != 0 {
                tcp::pm_metal_net_ip_tcp_ssl_opts_t {
                    sni: (*f).host.as_ptr() as *const c_char,
                    insecure: 0,
                    ca_pem: core::ptr::null(),
                    ca_pem_len: 0,
                }
            } else {
                tcp::pm_metal_net_ip_tcp_ssl_opts_t {
                    sni: core::ptr::null(),
                    insecure: 0,
                    ca_pem: core::ptr::null(),
                    ca_pem_len: 0,
                }
            };
            let ssl_ptr = if (*f).use_ssl != 0 {
                &opts as *const _
            } else {
                core::ptr::null()
            };
            (*f).child_h = tcp::pm_metal_net_ip_tcp_connect(
                (*f).host.as_ptr() as *const c_char,
                (*f).port,
                ssl_ptr,
            );
            if (*f).child_h == 0 {
                return finish(f, self_h, false);
            }
            (*f).phase = PHASE_CONNECT_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        PHASE_CONNECT_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => finish(f, self_h, false),
            Child::Done(ch) => {
                if ch == 0 {
                    return finish(f, self_h, false);
                }
                (*f).conn_h = ch;
                (*f).phase = PHASE_SEND;
                coro::PENDING
            }
        },
        PHASE_SEND => {
            if !build_request(f) {
                return finish(f, self_h, false);
            }
            let left = (*f).request_len - (*f).request_off;
            (*f).child_h = tcp::pm_metal_net_ip_tcp_write(
                (*f).conn_h,
                (*f).request.as_ptr().add((*f).request_off as usize),
                left,
            );
            if (*f).child_h == 0 {
                return finish(f, self_h, false);
            }
            (*f).phase = PHASE_SEND_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        PHASE_SEND_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => finish(f, self_h, false),
            Child::Done(n) => {
                if n == 0 {
                    return finish(f, self_h, false);
                }
                (*f).request_off += n;
                if (*f).request_off < (*f).request_len {
                    (*f).phase = PHASE_SEND;
                    return coro::PENDING;
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + HTTP_IDLE_US;
                (*f).phase = PHASE_RECV;
                coro::PENDING
            }
        },
        PHASE_RECV => {
            if (*f).complete != 0 {
                return finish(f, self_h, true);
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                return finish(f, self_h, (*f).headers_done != 0 && (*f).body_len > 0);
            }
            (*f).child_h = tcp::pm_metal_net_ip_tcp_read(
                (*f).conn_h,
                (*f).chunk.as_mut_ptr(),
                HTTP_CHUNK as u32,
            );
            if (*f).child_h == 0 {
                return finish(f, self_h, false);
            }
            (*f).phase = PHASE_RECV_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        PHASE_RECV_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => finish(f, self_h, false),
            Child::Done(n) => {
                if n == 0 {
                    (*f).remote_closed = 1;
                    let ok = (*f).headers_done != 0
                        && ((*f).have_content_len == 0
                            || (*f).body_len >= (*f).content_len
                            || (*f).body_len >= (*f).dest_cap);
                    return finish(f, self_h, ok);
                }
                if (*f).header_len >= HTTP_HEADER_MAX as u32 && (*f).headers_done == 0 {
                    return finish(f, self_h, false);
                }
                feed(f, (*f).chunk.as_ptr(), n);
                if (*f).header_len >= HTTP_HEADER_MAX as u32 && (*f).headers_done == 0 {
                    return finish(f, self_h, false);
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + HTTP_IDLE_US;
                (*f).phase = PHASE_RECV;
                coro::PENDING
            }
        },
        _ => finish(f, self_h, false),
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_get(
    url: *const c_char,
    dest: *mut c_void,
    dest_cap: u32,
) -> u32 {
    if url.is_null() || dest.is_null() || dest_cap == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<HttpFrame>(http_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<HttpFrame>(h);
    if !coro::copy_cstr(&mut (*f).url, url) {
        coro::pm_metal_async_coro_close(h);
        return 0;
    }
    (*f).phase = PHASE_PARSE;
    (*f).dest = dest as *mut u8;
    (*f).dest_cap = dest_cap;
    LAST_VALID.store(false, Ordering::Relaxed);
    h
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_status(h: u32) -> u32 {
    let f = coro::frame::<HttpFrame>(h);
    if !f.is_null() {
        return (*f).status;
    }
    if LAST_VALID.load(Ordering::Relaxed) {
        LAST_STATUS.load(Ordering::Relaxed)
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_body_len(h: u32) -> u32 {
    let f = coro::frame::<HttpFrame>(h);
    if !f.is_null() {
        return (*f).body_len;
    }
    if LAST_VALID.load(Ordering::Relaxed) {
        LAST_BODY_LEN.load(Ordering::Relaxed)
    } else {
        0
    }
}

/// Loopback GET /health after httpd autoload. Logs `httpd: health ok`. Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_proof_health() -> i32 {
    extern "C" {
        fn pm_metal_async_status(h: u32) -> u32;
        fn pm_metal_async_create_task(h: u32) -> u32;
        fn pm_metal_async_run_poll_all() -> i32;
        fn pm_metal_net_ip_poll();
        fn pm_metal_time_mono_us() -> u64;
    }
    const ASYNC_DONE: u32 = 2;
    const ASYNC_ERROR: u32 = 4;
    const ASYNC_CANCELLED: u32 = 3;
    let mut body = [0u8; 64];
    let h = pm_metal_net_http_get(
        b"http://127.0.0.1/health\0".as_ptr() as *const c_char,
        body.as_mut_ptr() as *mut c_void,
        body.len() as u32,
    );
    if h == 0 {
        return -1;
    }
    if pm_metal_async_create_task(h) == 0 {
        coro::pm_metal_async_coro_close(h);
        return -1;
    }
    let deadline = pm_metal_time_mono_us() + 10_000_000;
    loop {
        let _ = pm_metal_async_run_poll_all();
        pm_metal_net_ip_poll();
        match pm_metal_async_status(h) {
            ASYNC_DONE => break,
            ASYNC_ERROR | ASYNC_CANCELLED => return -1,
            _ => {
                if pm_metal_time_mono_us() >= deadline {
                    return -1;
                }
            }
        }
    }
    let st = pm_metal_net_http_status(0);
    let blen = pm_metal_net_http_body_len(0);
    if st != 200 || blen < 2 {
        return -1;
    }
    if body[0] != b'o' || body[1] != b'k' {
        return -1;
    }
    0
}
