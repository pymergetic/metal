//! FTP client (passive EPSV/PASV RETR/STOR) and memory-root server on the
//! opaque TCP stream (read/write/close after connect/accept).
//!
//! Client `use_tls`: 0 = cleartext on port 21; non-zero = implicit FTPS on port 21
//! with SSL at connect (control + data both use tcp_connect SSL opts).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};
use core::ptr;
use core::sync::atomic::{AtomicBool, AtomicU16, AtomicU32, Ordering};

use pymergetic_metal_async as _;
use pymergetic_metal_net_ip as ip;
use pymergetic_metal_net_ip::coro::{self, Child};
use pymergetic_metal_net_ip_tcp as tcp;
use pymergetic_metal_rt as _;

const FTP_PORT: u16 = 21;
const HOST_MAX: usize = 128;
const PATH_MAX: usize = 256;
const LINE_MAX: usize = 512;
const CMD_MAX: usize = 384;
const CHUNK: usize = 4096;
const IO_US: u64 = 30_000_000;
const ROOT_MAX: usize = 16;
const SERVER_MAX: usize = 2;
const PASSIVE_BASE: u16 = 50000;

const C_CONNECT: u32 = 0;
const C_CONNECT_WAIT: u32 = 1;
const C_READ: u32 = 2;
const C_READ_WAIT: u32 = 3;
const C_WRITE: u32 = 4;
const C_WRITE_WAIT: u32 = 5;
const C_DATA_CONNECT: u32 = 6;
const C_DATA_CONNECT_WAIT: u32 = 7;
const C_XFER: u32 = 8;
const C_XFER_WAIT: u32 = 9;

const CS_GREET: u32 = 0;
const CS_USER: u32 = 1;
const CS_PASS: u32 = 2;
const CS_TYPE: u32 = 3;
const CS_EPSV: u32 = 4;
const CS_PASV: u32 = 5;
const CS_XFER_CMD: u32 = 6;
const CS_COMPLETE: u32 = 7;
const CS_QUIT: u32 = 8;

const OP_GET: u32 = 0;
const OP_PUT: u32 = 1;

#[repr(C)]
struct RootEntry {
    used: bool,
    path: [u8; PATH_MAX],
    data: *mut u8,
    cap: u32,
    len: u32,
    writable: bool,
}

static mut ROOT: [RootEntry; ROOT_MAX] = unsafe { core::mem::zeroed() };

struct FtpServer {
    used: bool,
    listen_h: u32,
    coro_h: u32,
}

extern "C" {
    fn pm_metal_async_create_task(h: u32) -> u32;
}

static mut SERVERS: [FtpServer; SERVER_MAX] = unsafe { core::mem::zeroed() };

static PASSIVE_PORT: AtomicU16 = AtomicU16::new(PASSIVE_BASE);
static LAST_VALID: AtomicBool = AtomicBool::new(false);
static LAST_LEN: AtomicU32 = AtomicU32::new(0);

#[repr(C)]
struct ClientFrame {
    phase: u32,
    script: u32,
    op: u32,
    host: [u8; HOST_MAX],
    path: [u8; PATH_MAX],
    use_tls: i32,
    child_h: u32,
    ctrl_h: u32,
    data_h: u32,
    dest: *mut u8,
    dest_cap: u32,
    src: *const u8,
    src_len: u32,
    got: u32,
    expect_code: u32,
    passive_port: u16,
    line: [u8; LINE_MAX],
    line_len: u32,
    pending: [u8; CMD_MAX],
    pending_len: u32,
    pending_off: u32,
    chunk: [u8; CHUNK],
    deadline: u64,
}

#[repr(C)]
struct ServerFrame {
    phase: u32,
    srv_h: u32,
    child_h: u32,
    ctrl_h: u32,
    creds_h: u32,
    logged_in: u32,
    passive_listen_h: u32,
    passive_port: u16,
    data_h: u32,
    pending_close: u32,
    root_idx: i32,
    xfer_off: u32,
    line: [u8; LINE_MAX],
    line_len: u32,
    reply: [u8; LINE_MAX],
    reply_len: u32,
    reply_off: u32,
    chunk: [u8; CHUNK],
    deadline: u64,
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
    fn cstr(&mut self, src: &[u8]) {
        let n = src.iter().position(|&b| b == 0).unwrap_or(src.len());
        self.bytes(&src[..n]);
    }
    fn byte(&mut self, b: u8) {
        self.bytes(&[b]);
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

unsafe fn line_reset(line: &mut [u8; LINE_MAX], line_len: &mut u32) {
    *line_len = 0;
    line[0] = 0;
}

unsafe fn line_append(line: &mut [u8; LINE_MAX], line_len: &mut u32, data: *const u8, n: u32) -> bool {
    for i in 0..n as usize {
        if *line_len as usize + 1 >= LINE_MAX {
            return false;
        }
        line[*line_len as usize] = *data.add(i);
        *line_len += 1;
        line[*line_len as usize] = 0;
        if *line_len >= 2
            && line[(*line_len as usize) - 2] == b'\r'
            && line[(*line_len as usize) - 1] == b'\n'
        {
            return true;
        }
    }
    false
}

unsafe fn line_code(line: &[u8; LINE_MAX], line_len: u32) -> u32 {
    if line_len < 3 {
        return 0;
    }
    let mut code = 0u32;
    for i in 0..3 {
        let b = line[i as usize];
        if !b.is_ascii_digit() {
            return 0;
        }
        code = code * 10 + (b - b'0') as u32;
    }
    code
}

unsafe fn line_is_final(line: &[u8; LINE_MAX], line_len: u32) -> bool {
    let n = line_len as usize;
    n >= 5 && line[3].is_ascii_digit() && line[4] == b' ' && line[n - 2] == b'\r' && line[n - 1] == b'\n'
}

unsafe fn normalize_path(dst: &mut [u8; PATH_MAX], src: &[u8]) -> bool {
    let mut start = 0usize;
    while start < src.len() && src[start] == b'/' {
        start += 1;
    }
    let tail = &src[start..];
    let end = tail.iter().position(|&b| b == 0).unwrap_or(tail.len());
    if end == 0 || end >= PATH_MAX {
        return false;
    }
    dst[..end].copy_from_slice(&tail[..end]);
    dst[end] = 0;
    true
}

unsafe fn root_find(path: &[u8]) -> i32 {
    let mut norm = [0u8; PATH_MAX];
    if !normalize_path(&mut norm, path) {
        return -1;
    }
    for i in 0..ROOT_MAX {
        if !ROOT[i].used {
            continue;
        }
        let mut eq = true;
        for j in 0..PATH_MAX {
            if ROOT[i].path[j] != norm[j] {
                eq = false;
                break;
            }
            if norm[j] == 0 {
                break;
            }
        }
        if eq {
            return i as i32;
        }
    }
    -1
}

unsafe fn client_ssl_opts(host: &[u8]) -> tcp::pm_metal_net_ip_tcp_ssl_opts_t {
    tcp::pm_metal_net_ip_tcp_ssl_opts_t {
        sni: host.as_ptr() as *const c_char,
        insecure: 0,
        ca_pem: ptr::null(),
        ca_pem_len: 0,
    }
}

unsafe fn queue_cmd(f: *mut ClientFrame, cmd: &[u8]) -> bool {
    if cmd.len() >= CMD_MAX {
        return false;
    }
    ptr::copy_nonoverlapping(cmd.as_ptr(), (*f).pending.as_mut_ptr(), cmd.len());
    (*f).pending_len = cmd.len() as u32;
    (*f).pending_off = 0;
    true
}

unsafe fn client_cleanup(f: *mut ClientFrame) {
    if (*f).data_h != 0 {
        tcp::pm_metal_net_ip_tcp_close((*f).data_h);
        (*f).data_h = 0;
    }
    if (*f).ctrl_h != 0 {
        tcp::pm_metal_net_ip_tcp_close((*f).ctrl_h);
        (*f).ctrl_h = 0;
    }
    if (*f).child_h != 0 {
        coro::pm_metal_async_coro_close((*f).child_h);
        (*f).child_h = 0;
    }
}

unsafe fn client_finish(f: *mut ClientFrame, self_h: u32, ok: bool) -> u32 {
    client_cleanup(f);
    LAST_VALID.store(true, Ordering::Relaxed);
    LAST_LEN.store((*f).got, Ordering::Relaxed);
    coro::pm_metal_async_set_result_u32(self_h, if ok { (*f).got } else { 0 });
    if ok { coro::DONE } else { coro::ERROR }
}

unsafe fn parse_epsv_line(f: *mut ClientFrame) -> bool {
    let line = &(*f).line;
    let n = (*f).line_len as usize;
    let mut bars = 0usize;
    let mut i = 0usize;
    while i < n {
        if line[i] == b'|' {
            bars += 1;
            if bars == 3 {
                i += 1;
                break;
            }
        }
        i += 1;
    }
    let mut port = 0u16;
    let mut digits = 0;
    while i < n && line[i].is_ascii_digit() {
        port = port * 10 + (line[i] - b'0') as u16;
        digits += 1;
        i += 1;
    }
    if digits == 0 || port == 0 {
        return false;
    }
    (*f).passive_port = port;
    true
}

unsafe fn parse_pasv_line(f: *mut ClientFrame) -> bool {
    let line = &(*f).line;
    let n = (*f).line_len as usize;
    let mut nums = [0u16; 6];
    let mut ni = 0usize;
    let mut i = 0usize;
    while i < n && ni < 6 {
        if line[i].is_ascii_digit() {
            let mut v = 0u16;
            while i < n && line[i].is_ascii_digit() {
                v = v * 10 + (line[i] - b'0') as u16;
                i += 1;
            }
            nums[ni] = v;
            ni += 1;
            continue;
        }
        i += 1;
    }
    if ni < 6 {
        return false;
    }
    (*f).passive_port = nums[4] * 256 + nums[5];
    (*f).passive_port != 0
}

unsafe fn xfer_cmd_bytes(path: &[u8], op: u32) -> Option<[u8; CMD_MAX]> {
    let mut cmd = [0u8; CMD_MAX];
    let mut w = Writer::new(&mut cmd);
    if op == OP_GET {
        w.bytes(b"RETR ");
    } else {
        w.bytes(b"STOR ");
    }
    w.cstr(path);
    w.bytes(b"\r\n");
    let len = w.finish()?;
    cmd[len] = 0;
    Some(cmd)
}

unsafe fn client_begin_xfer_cmd(f: *mut ClientFrame) -> bool {
    let cmd = match xfer_cmd_bytes(&(*f).path, (*f).op) {
        Some(c) => c,
        None => return false,
    };
    let len = coro::cstr_len(cmd.as_ptr() as *const c_char, CMD_MAX);
    queue_cmd(f, &cmd[..len])
}

unsafe fn client_goto_write(f: *mut ClientFrame, script: u32, expect: u32) -> u32 {
    (*f).script = script;
    (*f).expect_code = expect;
    (*f).phase = C_WRITE;
    coro::PENDING
}

unsafe fn client_after_line(f: *mut ClientFrame, self_h: u32) -> u32 {
    let code = line_code(&(*f).line, (*f).line_len);
    match (*f).script {
        CS_GREET => {
            if code != 220 {
                return client_finish(f, self_h, false);
            }
            if !queue_cmd(f, b"USER anonymous\r\n") {
                return client_finish(f, self_h, false);
            }
            client_goto_write(f, CS_USER, 331)
        }
        CS_USER => {
            if code == 230 {
                if !queue_cmd(f, b"TYPE I\r\n") {
                    return client_finish(f, self_h, false);
                }
                return client_goto_write(f, CS_TYPE, 200);
            }
            if code != 331 {
                return client_finish(f, self_h, false);
            }
            if !queue_cmd(f, b"PASS guest@\r\n") {
                return client_finish(f, self_h, false);
            }
            client_goto_write(f, CS_PASS, 230)
        }
        CS_PASS => {
            if code != 230 {
                return client_finish(f, self_h, false);
            }
            if !queue_cmd(f, b"TYPE I\r\n") {
                return client_finish(f, self_h, false);
            }
            client_goto_write(f, CS_TYPE, 200)
        }
        CS_TYPE => {
            if code != 200 {
                return client_finish(f, self_h, false);
            }
            if !queue_cmd(f, b"EPSV\r\n") {
                return client_finish(f, self_h, false);
            }
            client_goto_write(f, CS_EPSV, 229)
        }
        CS_EPSV => {
            if code == 229 && parse_epsv_line(f) {
                if !client_begin_xfer_cmd(f) {
                    return client_finish(f, self_h, false);
                }
                return client_goto_write(f, CS_XFER_CMD, 150);
            }
            if !queue_cmd(f, b"PASV\r\n") {
                return client_finish(f, self_h, false);
            }
            client_goto_write(f, CS_PASV, 227)
        }
        CS_PASV => {
            if code != 227 || !parse_pasv_line(f) {
                return client_finish(f, self_h, false);
            }
            if !client_begin_xfer_cmd(f) {
                return client_finish(f, self_h, false);
            }
            client_goto_write(f, CS_XFER_CMD, 150)
        }
        CS_XFER_CMD => {
            if code != 150 && code != 125 {
                return client_finish(f, self_h, false);
            }
            (*f).phase = C_DATA_CONNECT;
            coro::PENDING
        }
        CS_COMPLETE => {
            if code != 226 && code != 250 {
                return client_finish(f, self_h, false);
            }
            if !queue_cmd(f, b"QUIT\r\n") {
                return client_finish(f, self_h, true);
            }
            client_goto_write(f, CS_QUIT, 221)
        }
        CS_QUIT => client_finish(f, self_h, true),
        _ => client_finish(f, self_h, false),
    }
}

unsafe fn client_on_final_line(f: *mut ClientFrame, self_h: u32) -> u32 {
    let code = line_code(&(*f).line, (*f).line_len);
    let script = (*f).script;
    let expect = (*f).expect_code;
    let ok = (script == CS_GREET && code == 220)
        || (script == CS_USER && (code == 331 || code == 230))
        || (script == CS_PASS && code == 230)
        || (script == CS_TYPE && code == 200)
        || (script == CS_EPSV && (code == 229 || code == 500))
        || (script == CS_PASV && code == 227)
        || (script == CS_XFER_CMD && (code == 150 || code == 125))
        || (script == CS_COMPLETE && (code == 226 || code == 250))
        || (script == CS_QUIT && code == 221)
        || code == expect;
    if !ok {
        return client_finish(f, self_h, false);
    }
    line_reset(&mut (*f).line, &mut (*f).line_len);
    client_after_line(f, self_h)
}

unsafe extern "C" fn client_step(self_h: u32) -> u32 {
    let f = coro::frame::<ClientFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }

    match (*f).phase {
        C_CONNECT => {
            let ssl_ptr = if (*f).use_tls != 0 {
                &client_ssl_opts(&(*f).host) as *const _
            } else {
                ptr::null()
            };
            (*f).child_h = tcp::pm_metal_net_ip_tcp_connect(
                (*f).host.as_ptr() as *const c_char,
                FTP_PORT,
                ssl_ptr,
            );
            if (*f).child_h == 0 {
                return client_finish(f, self_h, false);
            }
            (*f).phase = C_CONNECT_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        C_CONNECT_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => client_finish(f, self_h, false),
            Child::Done(ch) => {
                if ch == 0 {
                    return client_finish(f, self_h, false);
                }
                (*f).ctrl_h = ch;
                (*f).script = CS_GREET;
                (*f).expect_code = 220;
                line_reset(&mut (*f).line, &mut (*f).line_len);
                (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
                (*f).phase = C_READ;
                coro::PENDING
            }
        },
        C_READ => {
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                return client_finish(f, self_h, false);
            }
            (*f).child_h = tcp::pm_metal_net_ip_tcp_read(
                (*f).ctrl_h,
                (*f).chunk.as_mut_ptr(),
                CHUNK as u32,
            );
            if (*f).child_h == 0 {
                return client_finish(f, self_h, false);
            }
            (*f).phase = C_READ_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        C_READ_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => client_finish(f, self_h, false),
            Child::Done(n) => {
                if n == 0 && (*f).script != CS_COMPLETE {
                    return client_finish(f, self_h, false);
                }
                if n > 0
                    && !line_append(
                        &mut (*f).line,
                        &mut (*f).line_len,
                        (*f).chunk.as_ptr(),
                        n,
                    )
                {
                    return client_finish(f, self_h, false);
                }
                if line_is_final(&(*f).line, (*f).line_len) {
                    return client_on_final_line(f, self_h);
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
                (*f).phase = C_READ;
                coro::PENDING
            }
        },
        C_WRITE => {
            if (*f).pending_off >= (*f).pending_len {
                (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
                (*f).phase = C_READ;
                return coro::PENDING;
            }
            let left = (*f).pending_len - (*f).pending_off;
            (*f).child_h = tcp::pm_metal_net_ip_tcp_write(
                (*f).ctrl_h,
                (*f).pending.as_ptr().add((*f).pending_off as usize),
                left,
            );
            if (*f).child_h == 0 {
                return client_finish(f, self_h, false);
            }
            (*f).phase = C_WRITE_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        C_WRITE_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => client_finish(f, self_h, false),
            Child::Done(n) => {
                if n == 0 {
                    return client_finish(f, self_h, false);
                }
                (*f).pending_off += n;
                (*f).phase = C_WRITE;
                coro::PENDING
            }
        },
        C_DATA_CONNECT => {
            let ssl_ptr = if (*f).use_tls != 0 {
                &client_ssl_opts(&(*f).host) as *const _
            } else {
                ptr::null()
            };
            (*f).child_h = tcp::pm_metal_net_ip_tcp_connect(
                (*f).host.as_ptr() as *const c_char,
                (*f).passive_port,
                ssl_ptr,
            );
            if (*f).child_h == 0 {
                return client_finish(f, self_h, false);
            }
            (*f).phase = C_DATA_CONNECT_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        C_DATA_CONNECT_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => client_finish(f, self_h, false),
            Child::Done(ch) => {
                if ch == 0 {
                    return client_finish(f, self_h, false);
                }
                (*f).data_h = ch;
                (*f).phase = C_XFER;
                coro::PENDING
            }
        },
        C_XFER => {
            if (*f).op == OP_GET {
                if (*f).got >= (*f).dest_cap {
                    tcp::pm_metal_net_ip_tcp_close((*f).data_h);
                    (*f).data_h = 0;
                    (*f).script = CS_COMPLETE;
                    (*f).expect_code = 226;
                    (*f).phase = C_READ;
                    return coro::PENDING;
                }
                (*f).child_h = tcp::pm_metal_net_ip_tcp_read(
                    (*f).data_h,
                    (*f).chunk.as_mut_ptr(),
                    core::cmp::min(CHUNK as u32, (*f).dest_cap - (*f).got),
                );
            } else if (*f).got >= (*f).src_len {
                tcp::pm_metal_net_ip_tcp_close((*f).data_h);
                (*f).data_h = 0;
                (*f).script = CS_COMPLETE;
                (*f).expect_code = 226;
                (*f).phase = C_READ;
                return coro::PENDING;
            } else {
                let left = (*f).src_len - (*f).got;
                (*f).child_h = tcp::pm_metal_net_ip_tcp_write(
                    (*f).data_h,
                    (*f).src.add((*f).got as usize),
                    core::cmp::min(CHUNK as u32, left),
                );
            }
            if (*f).child_h == 0 {
                return client_finish(f, self_h, false);
            }
            (*f).phase = C_XFER_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        C_XFER_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => client_finish(f, self_h, false),
            Child::Done(n) => {
                if n == 0 {
                    if (*f).op == OP_GET {
                        tcp::pm_metal_net_ip_tcp_close((*f).data_h);
                        (*f).data_h = 0;
                        (*f).script = CS_COMPLETE;
                        (*f).expect_code = 226;
                        (*f).phase = C_READ;
                        return coro::PENDING;
                    }
                    return client_finish(f, self_h, false);
                }
                if (*f).op == OP_GET {
                    for i in 0..n as usize {
                        *(*f).dest.add((*f).got as usize + i) = (*f).chunk[i];
                    }
                }
                (*f).got += n;
                (*f).phase = C_XFER;
                coro::PENDING
            }
        },
        _ => client_finish(f, self_h, false),
    }
}

unsafe fn server_ipv4_octets() -> [u8; 4] {
    let mut buf = [0u8; 160];
    if ip::pm_metal_net_ip_if_status_index(0, buf.as_mut_ptr() as *mut c_char, buf.len() as u32) >= 0 {
        let n = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
        let text = &buf[..n];
        if let Some(sp) = text.iter().position(|&b| b == b' ') {
            let rest = &text[sp + 1..];
            let end = rest
                .iter()
                .position(|&b| b == b'/' || b == b' ')
                .unwrap_or(rest.len());
            let ip = &rest[..end];
            let mut oct = [127u8, 0, 0, 1];
            let mut idx = 0usize;
            let mut part = 0u16;
            for &b in ip {
                if b == b'.' {
                    if idx < 4 {
                        oct[idx] = part as u8;
                        idx += 1;
                    }
                    part = 0;
                } else if b.is_ascii_digit() {
                    part = part * 10 + (b - b'0') as u16;
                }
            }
            if idx == 3 {
                oct[3] = part as u8;
                return oct;
            }
        }
    }
    [127, 0, 0, 1]
}

unsafe fn alloc_passive_listen() -> (u32, u16) {
    for _ in 0..256 {
        let mut port = PASSIVE_PORT.fetch_add(1, Ordering::Relaxed);
        if port < PASSIVE_BASE {
            port = PASSIVE_BASE;
            PASSIVE_PORT.store(PASSIVE_BASE + 1, Ordering::Relaxed);
        }
        let lh = tcp::pm_metal_net_ip_tcp_listen(port);
        if lh != 0 {
            return (lh, port);
        }
    }
    (0, 0)
}

unsafe fn queue_reply(f: *mut ServerFrame, msg: &[u8]) -> bool {
    if msg.len() >= LINE_MAX {
        return false;
    }
    ptr::copy_nonoverlapping(msg.as_ptr(), (*f).reply.as_mut_ptr(), msg.len());
    (*f).reply_len = msg.len() as u32;
    (*f).reply_off = 0;
    true
}

unsafe fn server_close_passive(f: *mut ServerFrame) {
    if (*f).passive_listen_h != 0 {
        tcp::pm_metal_net_ip_tcp_listen_close((*f).passive_listen_h);
        (*f).passive_listen_h = 0;
        (*f).passive_port = 0;
    }
}

unsafe fn server_close_data(f: *mut ServerFrame) {
    if (*f).data_h != 0 {
        tcp::pm_metal_net_ip_tcp_close((*f).data_h);
        (*f).data_h = 0;
    }
}

unsafe fn server_close_ctrl(f: *mut ServerFrame) {
    server_close_passive(f);
    server_close_data(f);
    if (*f).ctrl_h != 0 {
        tcp::pm_metal_net_ip_tcp_close((*f).ctrl_h);
        (*f).ctrl_h = 0;
    }
}

unsafe fn server_session_done(f: *mut ServerFrame) -> u32 {
    server_close_ctrl(f);
    (*f).logged_in = 0;
    (*f).root_idx = -1;
    (*f).xfer_off = 0;
    line_reset(&mut (*f).line, &mut (*f).line_len);
    (*f).phase = S_ACCEPT;
    coro::PENDING
}

const S_ACCEPT: u32 = 0;
const S_ACCEPT_WAIT: u32 = 1;
const S_GREET: u32 = 2;
const S_READ: u32 = 3;
const S_READ_WAIT: u32 = 4;
const S_SEND: u32 = 5;
const S_SEND_WAIT: u32 = 6;
const S_DATA_ACCEPT: u32 = 7;
const S_DATA_ACCEPT_WAIT: u32 = 8;
const S_DATA_XFER: u32 = 9;
const S_DATA_XFER_WAIT: u32 = 10;

unsafe fn cmd_token(line: &[u8; LINE_MAX], line_len: u32) -> ([u8; 8], usize) {
    let n = line_len as usize;
    let mut verb = [0u8; 8];
    let mut vi = 0usize;
    let mut i = 0usize;
    while i < n && (line[i] == b' ' || line[i] == b'\t') {
        i += 1;
    }
    while i < n && line[i] != b' ' && line[i] != b'\r' && line[i] != b'\n' {
        if vi < 8 {
            verb[vi] = line[i].to_ascii_uppercase();
            vi += 1;
        }
        i += 1;
    }
    (verb, i)
}

unsafe fn cmd_arg(line: &[u8; LINE_MAX], line_len: u32, start: usize) -> usize {
    let n = line_len as usize;
    let mut i = start;
    while i < n && (line[i] == b' ' || line[i] == b'\t') {
        i += 1;
    }
    i
}

unsafe fn verb_is(verb: [u8; 8], s: &[u8]) -> bool {
    verb[..s.len()] == *s && (s.len() >= 8 || verb[s.len()] == 0)
}

unsafe fn handle_server_cmd(f: *mut ServerFrame, _self_h: u32) -> u32 {
    let (verb, arg_start) = cmd_token(&(*f).line, (*f).line_len);
    if verb_is(verb, b"USER") {
        let _ = queue_reply(f, b"331 Password required\r\n");
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"PASS") {
        (*f).logged_in = 1;
        let _ = queue_reply(f, b"230 Login successful\r\n");
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"QUIT") {
        let _ = queue_reply(f, b"221 Goodbye\r\n");
        (*f).pending_close = 1;
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if (*f).logged_in == 0 {
        let _ = queue_reply(f, b"530 Please login\r\n");
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"SYST") {
        let _ = queue_reply(f, b"215 UNIX Type: L8\r\n");
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"TYPE") {
        let _ = queue_reply(f, b"200 Type set to I\r\n");
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"PWD") {
        let _ = queue_reply(f, b"257 \"/\" is current directory\r\n");
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"CWD") {
        let _ = queue_reply(f, b"250 Directory changed\r\n");
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"EPSV") {
        server_close_passive(f);
        let (lh, port) = alloc_passive_listen();
        if lh == 0 {
            let _ = queue_reply(f, b"425 Cannot open passive connection\r\n");
            (*f).phase = S_SEND;
            return coro::PENDING;
        }
        (*f).passive_listen_h = lh;
        (*f).passive_port = port;
        let mut msg = [0u8; 64];
        let mut w = Writer::new(&mut msg);
        w.bytes(b"229 Entering Extended Passive Mode (|||");
        w.dec(port as u32);
        w.bytes(b"|)\r\n");
        if let Some(len) = w.finish() {
            let _ = queue_reply(f, &msg[..len]);
        }
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"PASV") {
        server_close_passive(f);
        let (lh, port) = alloc_passive_listen();
        if lh == 0 {
            let _ = queue_reply(f, b"425 Cannot open passive connection\r\n");
            (*f).phase = S_SEND;
            return coro::PENDING;
        }
        (*f).passive_listen_h = lh;
        (*f).passive_port = port;
        let oct = server_ipv4_octets();
        let p1 = (port >> 8) as u8;
        let p2 = (port & 0xff) as u8;
        let mut msg = [0u8; 80];
        let mut w = Writer::new(&mut msg);
        w.bytes(b"227 Entering Passive Mode (");
        w.dec(oct[0] as u32);
        w.byte(b',');
        w.dec(oct[1] as u32);
        w.byte(b',');
        w.dec(oct[2] as u32);
        w.byte(b',');
        w.dec(oct[3] as u32);
        w.byte(b',');
        w.dec(p1 as u32);
        w.byte(b',');
        w.dec(p2 as u32);
        w.bytes(b")\r\n");
        if let Some(len) = w.finish() {
            let _ = queue_reply(f, &msg[..len]);
        }
        (*f).phase = S_SEND;
        return coro::PENDING;
    }
    if verb_is(verb, b"RETR") {
        let arg = cmd_arg(&(*f).line, (*f).line_len, arg_start);
        let idx = root_find(&(&(*f).line)[arg..]);
        if idx < 0 {
            let _ = queue_reply(f, b"550 File unavailable\r\n");
            (*f).phase = S_SEND;
            return coro::PENDING;
        }
        (*f).root_idx = idx;
        (*f).xfer_off = 0;
        let _ = queue_reply(f, b"150 Opening data connection\r\n");
        (*f).phase = S_DATA_ACCEPT;
        return coro::PENDING;
    }
    if verb_is(verb, b"STOR") {
        let arg = cmd_arg(&(*f).line, (*f).line_len, arg_start);
        let idx = root_find(&(&(*f).line)[arg..]);
        if idx < 0 || !ROOT[idx as usize].writable {
            let _ = queue_reply(f, b"550 File unavailable\r\n");
            (*f).phase = S_SEND;
            return coro::PENDING;
        }
        ROOT[idx as usize].len = 0;
        (*f).root_idx = idx;
        (*f).xfer_off = 0;
        let _ = queue_reply(f, b"150 Opening data connection\r\n");
        (*f).phase = S_DATA_ACCEPT;
        return coro::PENDING;
    }
    let _ = queue_reply(f, b"502 Command not implemented\r\n");
    (*f).phase = S_SEND;
    coro::PENDING
}

unsafe extern "C" fn server_step(self_h: u32) -> u32 {
    let f = coro::frame::<ServerFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }
    let srv_h = (*f).srv_h;
    if srv_h == 0 || (srv_h as usize) > SERVER_MAX || !SERVERS[(srv_h as usize) - 1].used {
        return coro::ERROR;
    }
    let listen_h = SERVERS[(srv_h as usize) - 1].listen_h;

    match (*f).phase {
        S_ACCEPT => {
            (*f).child_h = tcp::pm_metal_net_ip_tcp_accept(listen_h, (*f).creds_h);
            if (*f).child_h == 0 {
                return coro::ERROR;
            }
            (*f).phase = S_ACCEPT_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        S_ACCEPT_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => {
                (*f).phase = S_ACCEPT;
                coro::PENDING
            }
            Child::Done(ch) => {
                if ch == 0 {
                    (*f).phase = S_ACCEPT;
                    return coro::PENDING;
                }
                (*f).ctrl_h = ch;
                (*f).logged_in = 0;
                (*f).pending_close = 0;
                (*f).root_idx = -1;
                line_reset(&mut (*f).line, &mut (*f).line_len);
                let _ = queue_reply(f, b"220 Metal FTP ready\r\n");
                (*f).phase = S_GREET;
                coro::PENDING
            }
        },
        S_GREET => {
            (*f).phase = S_SEND;
            coro::PENDING
        }
        S_READ => {
            if (*f).pending_close != 0 {
                return server_session_done(f);
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                return server_session_done(f);
            }
            (*f).child_h = tcp::pm_metal_net_ip_tcp_read(
                (*f).ctrl_h,
                (*f).chunk.as_mut_ptr(),
                CHUNK as u32,
            );
            if (*f).child_h == 0 {
                return server_session_done(f);
            }
            (*f).phase = S_READ_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        S_READ_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => server_session_done(f),
            Child::Done(n) => {
                if n == 0 {
                    return server_session_done(f);
                }
                if !line_append(&mut (*f).line, &mut (*f).line_len, (*f).chunk.as_ptr(), n) {
                    return server_session_done(f);
                }
                if line_is_final(&(*f).line, (*f).line_len) {
                    return handle_server_cmd(f, self_h);
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
                (*f).phase = S_READ;
                coro::PENDING
            }
        },
        S_SEND => {
            if (*f).reply_off >= (*f).reply_len {
                line_reset(&mut (*f).line, &mut (*f).line_len);
                if (*f).pending_close != 0 {
                    return server_session_done(f);
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + IO_US;
                (*f).phase = S_READ;
                return coro::PENDING;
            }
            let left = (*f).reply_len - (*f).reply_off;
            (*f).child_h = tcp::pm_metal_net_ip_tcp_write(
                (*f).ctrl_h,
                (*f).reply.as_ptr().add((*f).reply_off as usize),
                left,
            );
            if (*f).child_h == 0 {
                return server_session_done(f);
            }
            (*f).phase = S_SEND_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        S_SEND_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => server_session_done(f),
            Child::Done(n) => {
                if n == 0 {
                    return server_session_done(f);
                }
                (*f).reply_off += n;
                (*f).phase = S_SEND;
                coro::PENDING
            }
        },
        S_DATA_ACCEPT => {
            if (*f).passive_listen_h == 0 {
                let _ = queue_reply(f, b"425 Use PASV first\r\n");
                (*f).phase = S_SEND;
                return coro::PENDING;
            }
            (*f).child_h = tcp::pm_metal_net_ip_tcp_accept((*f).passive_listen_h, (*f).creds_h);
            if (*f).child_h == 0 {
                return server_session_done(f);
            }
            (*f).phase = S_DATA_ACCEPT_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        S_DATA_ACCEPT_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => {
                let _ = queue_reply(f, b"426 Connection closed\r\n");
                server_close_passive(f);
                (*f).phase = S_SEND;
                coro::PENDING
            }
            Child::Done(ch) => {
                if ch == 0 {
                    let _ = queue_reply(f, b"426 Connection closed\r\n");
                    server_close_passive(f);
                    (*f).phase = S_SEND;
                    return coro::PENDING;
                }
                (*f).data_h = ch;
                server_close_passive(f);
                (*f).phase = S_DATA_XFER;
                coro::PENDING
            }
        },
        S_DATA_XFER => {
            let idx = (*f).root_idx;
            if idx < 0 {
                server_close_data(f);
                let _ = queue_reply(f, b"550 File unavailable\r\n");
                (*f).phase = S_SEND;
                return coro::PENDING;
            }
            let entry = &mut ROOT[idx as usize];
            if entry.data.is_null() {
                server_close_data(f);
                let _ = queue_reply(f, b"550 File unavailable\r\n");
                (*f).phase = S_SEND;
                return coro::PENDING;
            }
            if entry.writable {
                if (*f).xfer_off >= entry.cap {
                    server_close_data(f);
                    entry.len = (*f).xfer_off;
                    let _ = queue_reply(f, b"226 Transfer complete\r\n");
                    (*f).phase = S_SEND;
                    return coro::PENDING;
                }
                (*f).child_h = tcp::pm_metal_net_ip_tcp_read(
                    (*f).data_h,
                    (*f).chunk.as_mut_ptr(),
                    core::cmp::min(CHUNK as u32, entry.cap - (*f).xfer_off),
                );
            } else if (*f).xfer_off >= entry.len {
                server_close_data(f);
                let _ = queue_reply(f, b"226 Transfer complete\r\n");
                (*f).phase = S_SEND;
                return coro::PENDING;
            } else {
                let left = entry.len - (*f).xfer_off;
                (*f).child_h = tcp::pm_metal_net_ip_tcp_write(
                    (*f).data_h,
                    entry.data.add((*f).xfer_off as usize),
                    core::cmp::min(CHUNK as u32, left),
                );
            }
            if (*f).child_h == 0 {
                server_close_data(f);
                let _ = queue_reply(f, b"426 Transfer failed\r\n");
                (*f).phase = S_SEND;
                return coro::PENDING;
            }
            (*f).phase = S_DATA_XFER_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }
        S_DATA_XFER_WAIT => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => {
                server_close_data(f);
                let _ = queue_reply(f, b"426 Transfer failed\r\n");
                (*f).phase = S_SEND;
                coro::PENDING
            }
            Child::Done(n) => {
                if n == 0 {
                    let idx = (*f).root_idx;
                    if idx >= 0 {
                        let entry = &mut ROOT[idx as usize];
                        if entry.writable {
                            entry.len = (*f).xfer_off;
                        }
                    }
                    server_close_data(f);
                    let _ = queue_reply(f, b"226 Transfer complete\r\n");
                    (*f).phase = S_SEND;
                    return coro::PENDING;
                }
                let idx = (*f).root_idx;
                if idx >= 0 {
                    let entry = &mut ROOT[idx as usize];
                    if entry.writable {
                        for i in 0..n as usize {
                            if (*f).xfer_off + i as u32 >= entry.cap {
                                break;
                            }
                            *entry.data.add((*f).xfer_off as usize + i) = (*f).chunk[i];
                        }
                    }
                }
                (*f).xfer_off += n;
                (*f).phase = S_DATA_XFER;
                coro::PENDING
            }
        },
        _ => {
            (*f).phase = S_ACCEPT;
            coro::PENDING
        }
    }
}

unsafe fn start_client(
    op: u32,
    host: *const c_char,
    path: *const c_char,
    use_tls: i32,
    dest: *mut u8,
    dest_cap: u32,
    src: *const u8,
    src_len: u32,
) -> u32 {
    if host.is_null() || *host == 0 || path.is_null() || *path == 0 {
        return 0;
    }
    if op == OP_GET && (dest.is_null() || dest_cap == 0) {
        return 0;
    }
    if op == OP_PUT && (src.is_null() || src_len == 0) {
        return 0;
    }
    let h = coro::coro_with_frame::<ClientFrame>(client_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<ClientFrame>(h);
    if !coro::copy_cstr(&mut (*f).host, host) || !coro::copy_cstr(&mut (*f).path, path) {
        coro::pm_metal_async_coro_close(h);
        return 0;
    }
    (*f).phase = C_CONNECT;
    (*f).script = CS_GREET;
    (*f).op = op;
    (*f).use_tls = use_tls;
    (*f).dest = dest;
    (*f).dest_cap = dest_cap;
    (*f).src = src;
    (*f).src_len = src_len;
    (*f).got = 0;
    (*f).ctrl_h = 0;
    (*f).data_h = 0;
    (*f).child_h = 0;
    (*f).pending_len = 0;
    LAST_VALID.store(false, Ordering::Relaxed);
    h
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ftp_get(
    host: *const c_char,
    path: *const c_char,
    dest: *mut c_void,
    dest_cap: u32,
    use_tls: i32,
) -> u32 {
    start_client(
        OP_GET,
        host,
        path,
        use_tls,
        dest as *mut u8,
        dest_cap,
        ptr::null(),
        0,
    )
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ftp_put(
    host: *const c_char,
    path: *const c_char,
    src: *const c_void,
    len: u32,
    use_tls: i32,
) -> u32 {
    start_client(
        OP_PUT,
        host,
        path,
        use_tls,
        ptr::null_mut(),
        0,
        src as *const u8,
        len,
    )
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ftp_len(h: u32) -> u32 {
    let f = coro::frame::<ClientFrame>(h);
    if !f.is_null() {
        return (*f).got;
    }
    if LAST_VALID.load(Ordering::Relaxed) {
        LAST_LEN.load(Ordering::Relaxed)
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ftp_root_clear() {
    for i in 0..ROOT_MAX {
        ROOT[i].used = false;
        ROOT[i].path = [0; PATH_MAX];
        ROOT[i].data = ptr::null_mut();
        ROOT[i].cap = 0;
        ROOT[i].len = 0;
        ROOT[i].writable = false;
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ftp_root_add(
    path: *const c_char,
    data: *mut c_void,
    len: u32,
    writable: i32,
) -> i32 {
    if path.is_null() || *path == 0 || data.is_null() || len == 0 {
        return -1;
    }
    let mut raw = [0u8; PATH_MAX];
    let plen = coro::cstr_len(path, PATH_MAX);
    if plen == 0 || plen >= PATH_MAX {
        return -1;
    }
    for i in 0..plen {
        raw[i] = *path.add(i) as u8;
    }
    raw[plen] = 0;
    let mut tmp = [0u8; PATH_MAX];
    if !normalize_path(&mut tmp, &raw) {
        return -1;
    }
    if root_find(&tmp) >= 0 {
        return -1;
    }
    for i in 0..ROOT_MAX {
        if !ROOT[i].used {
            ROOT[i].used = true;
            ROOT[i].path = tmp;
            ROOT[i].data = data as *mut u8;
            ROOT[i].cap = len;
            ROOT[i].len = len;
            ROOT[i].writable = writable != 0;
            return 0;
        }
    }
    -1
}

unsafe fn server_slot(h: u32) -> *mut FtpServer {
    if h == 0 || (h as usize) > SERVER_MAX {
        return ptr::null_mut();
    }
    let s = &mut SERVERS[(h as usize) - 1];
    if !s.used {
        return ptr::null_mut();
    }
    s
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ftp_listen(port: u16, creds_h: u32) -> u32 {
    if port == 0 {
        return 0;
    }
    let mut slot = 0usize;
    let mut found = false;
    for i in 0..SERVER_MAX {
        if !SERVERS[i].used {
            slot = i;
            found = true;
            break;
        }
    }
    if !found {
        return 0;
    }
    let listen_h = tcp::pm_metal_net_ip_tcp_listen(port);
    if listen_h == 0 {
        return 0;
    }
    let coro_h = coro::coro_with_frame::<ServerFrame>(server_step);
    if coro_h == 0 {
        tcp::pm_metal_net_ip_tcp_listen_close(listen_h);
        return 0;
    }
    let sf = coro::frame::<ServerFrame>(coro_h);
    if sf.is_null() {
        coro::pm_metal_async_coro_close(coro_h);
        tcp::pm_metal_net_ip_tcp_listen_close(listen_h);
        return 0;
    }
    (*sf).phase = S_ACCEPT;
    (*sf).srv_h = (slot + 1) as u32;
    (*sf).child_h = 0;
    (*sf).ctrl_h = 0;
    (*sf).creds_h = creds_h;
    (*sf).logged_in = 0;
    (*sf).passive_listen_h = 0;
    (*sf).passive_port = 0;
    (*sf).data_h = 0;
    (*sf).pending_close = 0;
    (*sf).root_idx = -1;
    (*sf).xfer_off = 0;
    (*sf).deadline = 0;
    if pm_metal_async_create_task(coro_h) == 0 {
        coro::pm_metal_async_coro_close(coro_h);
        tcp::pm_metal_net_ip_tcp_listen_close(listen_h);
        return 0;
    }
    let srv_h = (slot + 1) as u32;
    SERVERS[slot] = FtpServer {
        used: true,
        listen_h,
        coro_h,
    };
    srv_h
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ftp_close(srv_h: u32) {
    let s = server_slot(srv_h);
    if s.is_null() {
        return;
    }
    if (*s).coro_h != 0 {
        coro::pm_metal_async_coro_close((*s).coro_h);
        (*s).coro_h = 0;
    }
    if (*s).listen_h != 0 {
        tcp::pm_metal_net_ip_tcp_listen_close((*s).listen_h);
        (*s).listen_h = 0;
    }
    (*s).used = false;
}
