//! TFTP client + in-memory server on net.ip (lwIP UDP, RFC 1350 octet mode).
//!
//! Client GET/PUT mirror DNS/UDP/timeout/retry handling. The server picks a fresh
//! transfer port for its first DATA/ACK packet, so follow-on packets use that port.
//!
//! `host` is required (name or dotted IPv4). NULL host for DHCP next-server is
//! refused; net.ip exposes no such lease field today.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

use pymergetic_metal_async as _;
use pymergetic_metal_net_dns as dns;
use pymergetic_metal_net_ip as ip;
use pymergetic_metal_net_ip::coro::{self, Child};
use pymergetic_metal_rt as _;

const TFTP_PORT: u16 = 69;
const TFTP_BLOCK: u16 = 512;
const TFTP_HDR: u16 = 4;
const TFTP_PKT_MAX: usize = (TFTP_HDR + TFTP_BLOCK) as usize;
const TFTP_PATH_MAX: usize = 192;
const TFTP_HOST_MAX: usize = 128;
const TFTP_TIMEOUT_US: u64 = 3_000_000;
const TFTP_RETRIES: u32 = 5;
const ROOT_MAX: usize = 16;
const SRV_MAX: usize = 4;

const OP_RRQ: u16 = 1;
const OP_WRQ: u16 = 2;
const OP_DATA: u16 = 3;
const OP_ACK: u16 = 4;
const OP_ERROR: u16 = 5;

const TFTP_ERR_NOT_FOUND: u16 = 1;
const TFTP_ERR_ACCESS: u16 = 2;
const TFTP_ERR_FULL: u16 = 3;

const MODE_GET: u32 = 0;
const MODE_PUT: u32 = 1;

const PHASE_RESOLVE: u32 = 0;
const PHASE_DNS_WAIT: u32 = 1;
const PHASE_OPEN: u32 = 2;
const PHASE_SEND: u32 = 3;
const PHASE_WAIT: u32 = 4;
const PHASE_SLEEP: u32 = 5;

const SRV_IDLE: u32 = 0;
const SRV_XFER: u32 = 1;
const SRV_SLEEP: u32 = 2;

/// `pm_metal_net_tftp_status` codes.
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_net_tftp_status_t {
    PM_METAL_NET_TFTP_OK = 0,
    PM_METAL_NET_TFTP_BUSY = 1,
    PM_METAL_NET_TFTP_ERR_ARGS = 3,
    PM_METAL_NET_TFTP_ERR_RESOLVE = 4,
    PM_METAL_NET_TFTP_ERR_SOCKET = 5,
    PM_METAL_NET_TFTP_ERR_SEND = 6,
    PM_METAL_NET_TFTP_ERR_PROTO = 7,
    PM_METAL_NET_TFTP_ERR_TIMEOUT = 8,
}

use pm_metal_net_tftp_status_t as TftpStatus;

#[repr(C)]
struct TftpFrame {
    mode: u32,
    phase: u32,
    host: [u8; TFTP_HOST_MAX],
    path: [u8; TFTP_PATH_MAX],
    child_h: u32,
    status: u32,
    buf: *mut u8,
    buf_len: u32,
    got: u32,
    server: ip::ip_addr_t,
    server_port: u16,
    expect_block: u16,
    sent_block: u16,
    last_short: u32,
    retries: u32,
    have_pkt: u32,
    last_block: u32,
    deadline: u64,
    rx: [u8; TFTP_PKT_MAX],
    rx_len: u16,
    pcb: *mut ip::udp_pcb,
}

struct RootEntry {
    used: bool,
    path: [u8; TFTP_PATH_MAX],
    data: *mut u8,
    cap: u32,
    got: u32,
    writable: bool,
}

static mut ROOT: [RootEntry; ROOT_MAX] = unsafe { core::mem::zeroed() };

struct ServerSlot {
    used: bool,
    coro_h: u32,
}

static mut SERVERS: [ServerSlot; SRV_MAX] = unsafe { core::mem::zeroed() };

#[repr(C)]
struct SrvFrame {
    phase: u32,
    child_h: u32,
    listen_pcb: *mut ip::udp_pcb,
    xfer_pcb: *mut ip::udp_pcb,
    stop: u32,
    active: u32,
    is_write: u32,
    client: ip::ip_addr_t,
    client_port: u16,
    root_idx: i32,
    expect_block: u16,
    sent_block: u16,
    offset: u32,
    last_short: u32,
    retries: u32,
    deadline: u64,
    have_pkt: u32,
    on_xfer: u32,
    rx: [u8; TFTP_PKT_MAX],
    rx_len: u16,
}

/// Last completed transfer, for callers that already closed the handle.
static LAST_VALID: AtomicBool = AtomicBool::new(false);
static LAST_STATUS: AtomicU32 = AtomicU32::new(TftpStatus::PM_METAL_NET_TFTP_BUSY as u32);
static LAST_LEN: AtomicU32 = AtomicU32::new(0);

unsafe fn path_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    for i in 0..a.len() {
        if a[i] != b[i] {
            return false;
        }
    }
    true
}

unsafe fn path_cstr(entry: &[u8; TFTP_PATH_MAX]) -> &[u8] {
    let n = entry
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(entry.len());
    &entry[..n]
}

unsafe fn root_find(path: &[u8], need_write: bool) -> i32 {
    for i in 0..ROOT_MAX {
        let e = &ROOT[i];
        if !e.used {
            continue;
        }
        if need_write && !e.writable {
            continue;
        }
        if path_eq(path_cstr(&e.path), path) {
            return i as i32;
        }
    }
    -1
}

unsafe fn cleanup_client(f: *mut TftpFrame) {
    if !(*f).pcb.is_null() {
        ip::udp_remove((*f).pcb);
        (*f).pcb = core::ptr::null_mut();
    }
}

unsafe fn finish_client(f: *mut TftpFrame, self_h: u32, status: TftpStatus) -> u32 {
    cleanup_client(f);
    let code = status as u32;
    (*f).status = code;
    LAST_VALID.store(true, Ordering::Relaxed);
    LAST_STATUS.store(code, Ordering::Relaxed);
    LAST_LEN.store((*f).got, Ordering::Relaxed);
    let ok = status == TftpStatus::PM_METAL_NET_TFTP_OK;
    coro::pm_metal_async_set_result_u32(self_h, ok as u32);
    if ok {
        coro::DONE
    } else {
        coro::ERROR
    }
}

unsafe fn pkt_opcode(rx: &[u8], rx_len: u16) -> u16 {
    if rx_len < 2 {
        return 0;
    }
    ((rx[0] as u16) << 8) | rx[1] as u16
}

unsafe fn pkt_block(rx: &[u8]) -> u16 {
    ((rx[2] as u16) << 8) | rx[3] as u16
}

unsafe extern "C" fn client_recv(
    arg: *mut c_void,
    _pcb: *mut ip::udp_pcb,
    p: *mut ip::pbuf,
    addr: *const ip::ip_addr_t,
    port: u16,
) {
    let f = arg as *mut TftpFrame;
    if f.is_null() || p.is_null() {
        if !p.is_null() {
            ip::pbuf_free(p);
        }
        return;
    }
    if (*f).have_pkt != 0 {
        ip::pbuf_free(p);
        return;
    }
    if !addr.is_null() {
        (*f).server = *addr;
        (*f).server_port = port;
    }
    let n = (*p).tot_len.min(TFTP_PKT_MAX as u16);
    ip::pbuf_copy_partial(p, (*f).rx.as_mut_ptr() as *mut c_void, n, 0);
    (*f).rx_len = n;
    (*f).have_pkt = 1;
    ip::pbuf_free(p);
}

/// `RRQ <path> 0 "octet" 0` or `WRQ <path> 0 "octet" 0`.
unsafe fn send_request(f: *mut TftpFrame, op: u16) -> bool {
    let frame = &mut *f;
    if frame.pcb.is_null() {
        return false;
    }
    let path_len = frame
        .path
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(frame.path.len());
    let total = 2 + path_len + 1 + 5 + 1;
    if path_len == 0 || total > TFTP_PKT_MAX {
        return false;
    }

    let p = ip::pbuf_alloc(ip::PBUF_TRANSPORT, total as u16, ip::PBUF_RAM);
    if p.is_null() {
        return false;
    }
    let out = (*p).payload as *mut u8;
    *out = (op >> 8) as u8;
    *out.add(1) = (op & 0xff) as u8;
    for i in 0..path_len {
        *out.add(2 + i) = frame.path[i];
    }
    *out.add(2 + path_len) = 0;
    for (i, b) in b"octet".iter().enumerate() {
        *out.add(3 + path_len + i) = *b;
    }
    *out.add(total - 1) = 0;

    let err = ip::udp_sendto(frame.pcb, p, &frame.server, TFTP_PORT);
    ip::pbuf_free(p);
    err == ip::ERR_OK
}

unsafe fn send_ack(f: *mut TftpFrame, block: u16) -> bool {
    if (*f).pcb.is_null() {
        return false;
    }
    let p = ip::pbuf_alloc(ip::PBUF_TRANSPORT, TFTP_HDR, ip::PBUF_RAM);
    if p.is_null() {
        return false;
    }
    let out = (*p).payload as *mut u8;
    *out = 0;
    *out.add(1) = OP_ACK as u8;
    *out.add(2) = (block >> 8) as u8;
    *out.add(3) = (block & 0xff) as u8;
    let err = ip::udp_sendto((*f).pcb, p, &(*f).server, (*f).server_port);
    ip::pbuf_free(p);
    err == ip::ERR_OK
}

unsafe fn send_data_client(f: *mut TftpFrame, block: u16) -> bool {
    let frame = &mut *f;
    if frame.pcb.is_null() || block == 0 {
        return false;
    }
    let off = (block as u32 - 1) * TFTP_BLOCK as u32;
    if off > frame.buf_len {
        return false;
    }
    let remain = frame.buf_len - off;
    let chunk = remain.min(TFTP_BLOCK as u32) as u16;
    let total = TFTP_HDR + chunk;
    let p = ip::pbuf_alloc(ip::PBUF_TRANSPORT, total, ip::PBUF_RAM);
    if p.is_null() {
        return false;
    }
    let out = (*p).payload as *mut u8;
    *out = 0;
    *out.add(1) = OP_DATA as u8;
    *out.add(2) = (block >> 8) as u8;
    *out.add(3) = (block & 0xff) as u8;
    for i in 0..chunk as usize {
        *out.add(TFTP_HDR as usize + i) = *frame.buf.add(off as usize + i);
    }
    let err = ip::udp_sendto(frame.pcb, p, &frame.server, frame.server_port);
    ip::pbuf_free(p);
    if err != ip::ERR_OK {
        return false;
    }
    frame.sent_block = block;
    frame.last_short = (chunk < TFTP_BLOCK) as u32;
    frame.got = off + chunk as u32;
    true
}

unsafe fn handle_data_get(f: *mut TftpFrame) -> bool {
    let frame = &mut *f;
    if frame.rx_len < TFTP_HDR {
        return false;
    }
    let op = pkt_opcode(&frame.rx, frame.rx_len);
    if op == OP_ERROR || op != OP_DATA {
        return false;
    }

    let block = pkt_block(&frame.rx);
    if block != frame.expect_block {
        if block.wrapping_add(1) == frame.expect_block && frame.expect_block > 1 {
            let _ = send_ack(f, frame.expect_block.wrapping_sub(1));
        }
        return true;
    }

    let data_len = frame.rx_len - TFTP_HDR;
    if frame.got + data_len as u32 > frame.buf_len {
        return false;
    }
    for i in 0..data_len as usize {
        *frame.buf.add(frame.got as usize + i) = frame.rx[TFTP_HDR as usize + i];
    }
    frame.got += data_len as u32;
    if !send_ack(f, block) {
        return false;
    }
    frame.expect_block = block.wrapping_add(1);
    frame.retries = 0;
    frame.last_block = (data_len < TFTP_BLOCK) as u32;
    true
}

unsafe fn handle_ack_put(f: *mut TftpFrame) -> bool {
    let frame = &mut *f;
    if frame.rx_len < TFTP_HDR {
        return false;
    }
    let op = pkt_opcode(&frame.rx, frame.rx_len);
    if op == OP_ERROR || op != OP_ACK {
        return false;
    }

    let block = pkt_block(&frame.rx);
    if block != frame.expect_block {
        return true;
    }

    frame.retries = 0;
    if frame.expect_block == 0 {
        if !send_data_client(f, 1) {
            return false;
        }
        frame.expect_block = 1;
        return true;
    }

    if frame.last_short != 0 && block == frame.sent_block {
        frame.last_block = 1;
        return true;
    }

    let next = block.wrapping_add(1);
    let off = (next as u32 - 1) * TFTP_BLOCK as u32;
    if off >= frame.buf_len {
        frame.last_block = 1;
        return true;
    }
    if !send_data_client(f, next) {
        return false;
    }
    frame.expect_block = next;
    true
}

unsafe fn client_timeout_retry(f: *mut TftpFrame) -> bool {
    if (*f).mode == MODE_GET {
        if (*f).expect_block == 1 {
            (*f).phase = PHASE_SEND;
        } else {
            let _ = send_ack(f, (*f).expect_block.wrapping_sub(1));
        }
        return true;
    }
    if (*f).expect_block == 0 {
        (*f).phase = PHASE_SEND;
        return true;
    }
    send_data_client(f, (*f).sent_block)
}

unsafe extern "C" fn tftp_client_step(self_h: u32) -> u32 {
    let f = coro::frame::<TftpFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }

    match (*f).phase {
        PHASE_RESOLVE => {
            if (*f).host[0] == 0 || (*f).path[0] == 0 {
                return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_ARGS);
            }
            (*f).expect_block = if (*f).mode == MODE_GET { 1 } else { 0 };
            (*f).sent_block = 0;
            (*f).server_port = TFTP_PORT;
            if let Some(literal) = ip::aton((*f).host.as_ptr() as *const c_char) {
                (*f).server = literal;
                (*f).phase = PHASE_OPEN;
                return coro::PENDING;
            }
            (*f).child_h = dns::pm_metal_net_dns((*f).host.as_ptr() as *const c_char);
            if (*f).child_h == 0 {
                return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_RESOLVE);
            }
            (*f).phase = PHASE_DNS_WAIT;
            coro::pm_metal_async_await(self_h, (*f).child_h)
        }

        PHASE_DNS_WAIT => {
            let ok = match coro::finish_child(self_h, &mut (*f).child_h) {
                Child::Waiting => return coro::WAITING,
                Child::Done(result) => result,
                Child::Failed => 0,
            };
            let Some(addr) = (if ok != 0 { dns::last_addr() } else { None }) else {
                return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_RESOLVE);
            };
            (*f).server = addr;
            (*f).phase = PHASE_OPEN;
            coro::PENDING
        }

        PHASE_OPEN => {
            (*f).pcb = ip::udp_new_ip_type(ip::IPADDR_TYPE_V4);
            if (*f).pcb.is_null()
                || ip::udp_bind((*f).pcb, &ip::IP_ADDR_ANY, 0) != ip::ERR_OK
            {
                return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_SOCKET);
            }
            ip::udp_recv((*f).pcb, Some(client_recv), f as *mut c_void);
            (*f).phase = PHASE_SEND;
            coro::PENDING
        }

        PHASE_SEND => {
            (*f).have_pkt = 0;
            let ok = if (*f).mode == MODE_GET {
                send_request(f, OP_RRQ)
            } else {
                send_request(f, OP_WRQ)
            };
            if !ok {
                return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_SEND);
            }
            (*f).deadline = coro::pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
            (*f).phase = PHASE_WAIT;
            coro::PENDING
        }

        PHASE_WAIT => {
            coro::pm_metal_net_ip_poll();
            if (*f).have_pkt != 0 {
                (*f).have_pkt = 0;
                let ok = if (*f).mode == MODE_GET {
                    handle_data_get(f)
                } else {
                    handle_ack_put(f)
                };
                if !ok {
                    return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_PROTO);
                }
                if (*f).last_block != 0 {
                    return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_OK);
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
                return coro::PENDING;
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                (*f).retries += 1;
                if (*f).retries > TFTP_RETRIES {
                    return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_TIMEOUT);
                }
                if !client_timeout_retry(f) {
                    return finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_SEND);
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
                if (*f).phase == PHASE_SEND {
                    return coro::PENDING;
                }
                return coro::PENDING;
            }
            (*f).phase = PHASE_SLEEP;
            match coro::start_sleep(self_h, &mut (*f).child_h, 2000) {
                Some(status) => status,
                None => finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_SOCKET),
            }
        }

        PHASE_SLEEP => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_SOCKET),
            Child::Done(_) => {
                (*f).phase = PHASE_WAIT;
                coro::PENDING
            }
        },

        _ => finish_client(f, self_h, TftpStatus::PM_METAL_NET_TFTP_ERR_SOCKET),
    }
}

unsafe fn start_client(
    mode: u32,
    host: *const c_char,
    path: *const c_char,
    buf: *mut u8,
    buf_len: u32,
) -> u32 {
    if host.is_null() || *host == 0 || path.is_null() || *path == 0 {
        return 0;
    }
    if buf.is_null() || buf_len == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<TftpFrame>(tftp_client_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<TftpFrame>(h);
    if !coro::copy_cstr(&mut (*f).host, host) || !coro::copy_cstr(&mut (*f).path, path) {
        coro::pm_metal_async_coro_close(h);
        return 0;
    }
    (*f).mode = mode;
    (*f).phase = PHASE_RESOLVE;
    (*f).status = TftpStatus::PM_METAL_NET_TFTP_BUSY as u32;
    (*f).buf = buf;
    (*f).buf_len = buf_len;
    (*f).got = 0;
    (*f).child_h = 0;
    (*f).retries = 0;
    (*f).have_pkt = 0;
    (*f).last_block = 0;
    (*f).last_short = 0;
    (*f).pcb = core::ptr::null_mut();
    LAST_VALID.store(false, Ordering::Relaxed);
    h
}

unsafe fn srv_slot(h: u32) -> *mut ServerSlot {
    if h == 0 || (h as usize) > SRV_MAX {
        return core::ptr::null_mut();
    }
    let s = &mut SERVERS[(h as usize) - 1];
    if !s.used {
        return core::ptr::null_mut();
    }
    s
}

unsafe fn srv_cleanup_xfer(f: *mut SrvFrame) {
    if !(*f).xfer_pcb.is_null() {
        ip::udp_remove((*f).xfer_pcb);
        (*f).xfer_pcb = core::ptr::null_mut();
    }
    (*f).active = 0;
    (*f).is_write = 0;
    (*f).root_idx = -1;
    (*f).expect_block = 0;
    (*f).sent_block = 0;
    (*f).offset = 0;
    (*f).last_short = 0;
    (*f).retries = 0;
    (*f).have_pkt = 0;
    (*f).on_xfer = 0;
}

unsafe fn srv_cleanup_all(f: *mut SrvFrame) {
    srv_cleanup_xfer(f);
    if !(*f).listen_pcb.is_null() {
        ip::udp_remove((*f).listen_pcb);
        (*f).listen_pcb = core::ptr::null_mut();
    }
}

unsafe fn srv_send_error(
    pcb: *mut ip::udp_pcb,
    addr: &ip::ip_addr_t,
    port: u16,
    code: u16,
    msg: &[u8],
) -> bool {
    if pcb.is_null() {
        return false;
    }
    let mlen = msg.len().min(128);
    let total = 4 + mlen + 1;
    let p = ip::pbuf_alloc(ip::PBUF_TRANSPORT, total as u16, ip::PBUF_RAM);
    if p.is_null() {
        return false;
    }
    let out = (*p).payload as *mut u8;
    *out = 0;
    *out.add(1) = OP_ERROR as u8;
    *out.add(2) = (code >> 8) as u8;
    *out.add(3) = (code & 0xff) as u8;
    for i in 0..mlen {
        *out.add(4 + i) = msg[i];
    }
    *out.add(4 + mlen) = 0;
    let err = ip::udp_sendto(pcb, p, addr, port);
    ip::pbuf_free(p);
    err == ip::ERR_OK
}

unsafe fn srv_send_ack(f: *mut SrvFrame, block: u16) -> bool {
    if (*f).xfer_pcb.is_null() {
        return false;
    }
    let p = ip::pbuf_alloc(ip::PBUF_TRANSPORT, TFTP_HDR, ip::PBUF_RAM);
    if p.is_null() {
        return false;
    }
    let out = (*p).payload as *mut u8;
    *out = 0;
    *out.add(1) = OP_ACK as u8;
    *out.add(2) = (block >> 8) as u8;
    *out.add(3) = (block & 0xff) as u8;
    let err = ip::udp_sendto(
        (*f).xfer_pcb,
        p,
        &(*f).client,
        (*f).client_port,
    );
    ip::pbuf_free(p);
    if err == ip::ERR_OK {
        (*f).sent_block = block;
    }
    err == ip::ERR_OK
}

unsafe fn srv_send_data(f: *mut SrvFrame, block: u16) -> bool {
    if (*f).xfer_pcb.is_null() || (*f).root_idx < 0 {
        return false;
    }
    let e = &mut ROOT[(*f).root_idx as usize];
    let off = (block as u32 - 1) * TFTP_BLOCK as u32;
    if off >= e.got {
        return false;
    }
    let remain = e.got - off;
    let chunk = remain.min(TFTP_BLOCK as u32) as u16;
    let total = TFTP_HDR + chunk;
    let p = ip::pbuf_alloc(ip::PBUF_TRANSPORT, total, ip::PBUF_RAM);
    if p.is_null() {
        return false;
    }
    let out = (*p).payload as *mut u8;
    *out = 0;
    *out.add(1) = OP_DATA as u8;
    *out.add(2) = (block >> 8) as u8;
    *out.add(3) = (block & 0xff) as u8;
    for i in 0..chunk as usize {
        *out.add(TFTP_HDR as usize + i) = *e.data.add(off as usize + i);
    }
    let err = ip::udp_sendto(
        (*f).xfer_pcb,
        p,
        &(*f).client,
        (*f).client_port,
    );
    ip::pbuf_free(p);
    if err != ip::ERR_OK {
        return false;
    }
    (*f).sent_block = block;
    (*f).last_short = (chunk < TFTP_BLOCK) as u32;
    (*f).offset = off + chunk as u32;
    true
}

unsafe fn srv_open_xfer(f: *mut SrvFrame) -> bool {
    (*f).xfer_pcb = ip::udp_new_ip_type(ip::IPADDR_TYPE_V4);
    if (*f).xfer_pcb.is_null() {
        return false;
    }
    if ip::udp_bind((*f).xfer_pcb, &ip::IP_ADDR_ANY, 0) != ip::ERR_OK {
        ip::udp_remove((*f).xfer_pcb);
        (*f).xfer_pcb = core::ptr::null_mut();
        return false;
    }
    ip::udp_recv((*f).xfer_pcb, Some(srv_xfer_recv), f as *mut c_void);
    true
}

unsafe fn parse_request(rx: &[u8], rx_len: u16) -> Option<(u16, &[u8])> {
    if rx_len < 4 {
        return None;
    }
    let op = pkt_opcode(rx, rx_len);
    if op != OP_RRQ && op != OP_WRQ {
        return None;
    }
    let mut i = 2usize;
    let start = i;
    while i < rx_len as usize && rx[i] != 0 {
        i += 1;
    }
    if i >= rx_len as usize {
        return None;
    }
    Some((op, &rx[start..i]))
}

unsafe fn srv_begin_rrq(f: *mut SrvFrame, path: &[u8]) -> bool {
    let idx = root_find(path, false);
    if idx < 0 {
        let _ = srv_open_xfer(f);
        if !(*f).xfer_pcb.is_null() {
            let _ = srv_send_error(
                (*f).xfer_pcb,
                &(*f).client,
                (*f).client_port,
                TFTP_ERR_NOT_FOUND,
                b"file not found",
            );
            srv_cleanup_xfer(f);
        }
        return false;
    }
    let e = &ROOT[idx as usize];
    if e.got == 0 {
        return false;
    }
    if !srv_open_xfer(f) {
        return false;
    }
    (*f).active = 1;
    (*f).is_write = 0;
    (*f).root_idx = idx;
    (*f).expect_block = 1;
    (*f).offset = 0;
    if !srv_send_data(f, 1) {
        srv_cleanup_xfer(f);
        return false;
    }
    (*f).expect_block = 1;
    (*f).deadline = coro::pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
    true
}

unsafe fn srv_begin_wrq(f: *mut SrvFrame, path: &[u8]) -> bool {
    let idx = root_find(path, true);
    if idx < 0 {
        let _ = srv_open_xfer(f);
        if !(*f).xfer_pcb.is_null() {
            let _ = srv_send_error(
                (*f).xfer_pcb,
                &(*f).client,
                (*f).client_port,
                TFTP_ERR_ACCESS,
                b"access denied",
            );
            srv_cleanup_xfer(f);
        }
        return false;
    }
    ROOT[idx as usize].got = 0;
    if !srv_open_xfer(f) {
        return false;
    }
    (*f).active = 1;
    (*f).is_write = 1;
    (*f).root_idx = idx;
    (*f).expect_block = 1;
    if !srv_send_ack(f, 0) {
        srv_cleanup_xfer(f);
        return false;
    }
    (*f).deadline = coro::pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
    true
}

unsafe fn srv_handle_ack(f: *mut SrvFrame) -> bool {
    if (*f).rx_len < TFTP_HDR {
        return false;
    }
    let op = pkt_opcode(&(*f).rx, (*f).rx_len);
    if op == OP_ERROR || op != OP_ACK {
        return false;
    }
    let block = pkt_block(&(*f).rx);
    if block != (*f).sent_block {
        return true;
    }
    (*f).retries = 0;
    if (*f).last_short != 0 {
        return true;
    }
    let next = block.wrapping_add(1);
    let e = &ROOT[(*f).root_idx as usize];
    let off = (next as u32 - 1) * TFTP_BLOCK as u32;
    if off >= e.got {
        return false;
    }
    if !srv_send_data(f, next) {
        return false;
    }
    (*f).expect_block = next;
    true
}

unsafe fn srv_handle_data(f: *mut SrvFrame) -> bool {
    if (*f).rx_len < TFTP_HDR || (*f).root_idx < 0 {
        return false;
    }
    let op = pkt_opcode(&(*f).rx, (*f).rx_len);
    if op == OP_ERROR || op != OP_DATA {
        return false;
    }
    let block = pkt_block(&(*f).rx);
    if block != (*f).expect_block {
        if block.wrapping_add(1) == (*f).expect_block && (*f).expect_block > 1 {
            let _ = srv_send_ack(f, (*f).expect_block.wrapping_sub(1));
        }
        return true;
    }
    let data_len = (*f).rx_len - TFTP_HDR;
    let e = &mut ROOT[(*f).root_idx as usize];
    if e.got + data_len as u32 > e.cap {
        let _ = srv_send_error(
            (*f).xfer_pcb,
            &(*f).client,
            (*f).client_port,
            TFTP_ERR_FULL,
            b"disk full",
        );
        return false;
    }
    for i in 0..data_len as usize {
        *e.data.add(e.got as usize + i) = (*f).rx[TFTP_HDR as usize + i];
    }
    e.got += data_len as u32;
    if !srv_send_ack(f, block) {
        return false;
    }
    (*f).retries = 0;
    (*f).expect_block = block.wrapping_add(1);
    (*f).last_short = (data_len < TFTP_BLOCK) as u32;
    true
}

unsafe fn srv_store_pkt(f: *mut SrvFrame, p: *mut ip::pbuf, addr: *const ip::ip_addr_t, port: u16, on_xfer: u32) {
    if (*f).have_pkt != 0 {
        ip::pbuf_free(p);
        return;
    }
    if !addr.is_null() {
        (*f).client = *addr;
        (*f).client_port = port;
    }
    let n = (*p).tot_len.min(TFTP_PKT_MAX as u16);
    ip::pbuf_copy_partial(p, (*f).rx.as_mut_ptr() as *mut c_void, n, 0);
    (*f).rx_len = n;
    (*f).have_pkt = 1;
    (*f).on_xfer = on_xfer;
    ip::pbuf_free(p);
}

unsafe extern "C" fn srv_listen_recv(
    arg: *mut c_void,
    _pcb: *mut ip::udp_pcb,
    p: *mut ip::pbuf,
    addr: *const ip::ip_addr_t,
    port: u16,
) {
    let f = arg as *mut SrvFrame;
    if f.is_null() || p.is_null() {
        if !p.is_null() {
            ip::pbuf_free(p);
        }
        return;
    }
    if (*f).active != 0 {
        ip::pbuf_free(p);
        return;
    }
    srv_store_pkt(f, p, addr, port, 0);
}

unsafe extern "C" fn srv_xfer_recv(
    arg: *mut c_void,
    _pcb: *mut ip::udp_pcb,
    p: *mut ip::pbuf,
    addr: *const ip::ip_addr_t,
    port: u16,
) {
    let f = arg as *mut SrvFrame;
    if f.is_null() || p.is_null() {
        if !p.is_null() {
            ip::pbuf_free(p);
        }
        return;
    }
    if (*f).active == 0 {
        ip::pbuf_free(p);
        return;
    }
    srv_store_pkt(f, p, addr, port, 1);
}

unsafe extern "C" fn srv_step(self_h: u32) -> u32 {
    let f = coro::frame::<SrvFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }

    if (*f).stop != 0 {
        srv_cleanup_all(f);
        coro::pm_metal_async_set_result_u32(self_h, 0);
        return coro::DONE;
    }

    match (*f).phase {
        SRV_IDLE => {
            coro::pm_metal_net_ip_poll();
            if (*f).have_pkt != 0 && (*f).active == 0 {
                (*f).have_pkt = 0;
                if let Some((op, path)) = parse_request(&(*f).rx, (*f).rx_len) {
                    let started = if op == OP_RRQ {
                        srv_begin_rrq(f, path)
                    } else {
                        srv_begin_wrq(f, path)
                    };
                    if started {
                        (*f).phase = SRV_XFER;
                        return coro::PENDING;
                    }
                }
            }
            (*f).phase = SRV_SLEEP;
            match coro::start_sleep(self_h, &mut (*f).child_h, 2000) {
                Some(status) => status,
                None => coro::ERROR,
            }
        }

        SRV_XFER => {
            coro::pm_metal_net_ip_poll();
            if (*f).have_pkt != 0 {
                (*f).have_pkt = 0;
                let done = if (*f).is_write != 0 {
                    let ok = srv_handle_data(f);
                    if !ok {
                        srv_cleanup_xfer(f);
                        (*f).phase = SRV_IDLE;
                        return coro::PENDING;
                    }
                    (*f).last_short != 0
                } else {
                    let ok = srv_handle_ack(f);
                    if !ok {
                        srv_cleanup_xfer(f);
                        (*f).phase = SRV_IDLE;
                        return coro::PENDING;
                    }
                    (*f).last_short != 0
                };
                if done {
                    srv_cleanup_xfer(f);
                    (*f).phase = SRV_IDLE;
                    return coro::PENDING;
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
                return coro::PENDING;
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                (*f).retries += 1;
                if (*f).retries > TFTP_RETRIES {
                    srv_cleanup_xfer(f);
                    (*f).phase = SRV_IDLE;
                    return coro::PENDING;
                }
                let ok = if (*f).is_write != 0 {
                    if (*f).sent_block != 0 {
                        srv_send_ack(f, (*f).sent_block)
                    } else {
                        srv_send_ack(f, 0)
                    }
                } else if (*f).sent_block != 0 {
                    srv_send_data(f, (*f).sent_block)
                } else {
                    false
                };
                if !ok {
                    srv_cleanup_xfer(f);
                    (*f).phase = SRV_IDLE;
                    return coro::PENDING;
                }
                (*f).deadline = coro::pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
                return coro::PENDING;
            }
            (*f).phase = SRV_SLEEP;
            match coro::start_sleep(self_h, &mut (*f).child_h, 2000) {
                Some(status) => status,
                None => coro::ERROR,
            }
        }

        SRV_SLEEP => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => coro::ERROR,
            Child::Done(_) => {
                (*f).phase = if (*f).active != 0 {
                    SRV_XFER
                } else {
                    SRV_IDLE
                };
                coro::PENDING
            }
        },

        _ => coro::ERROR,
    }
}

/// Clear all in-memory root entries.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_tftp_root_clear() {
    for i in 0..ROOT_MAX {
        ROOT[i].used = false;
        ROOT[i].path = [0; TFTP_PATH_MAX];
        ROOT[i].data = core::ptr::null_mut();
        ROOT[i].cap = 0;
        ROOT[i].got = 0;
        ROOT[i].writable = false;
    }
}

/// Register `path` in the memory root. `data` stays owned by the caller.
/// Read-only entries use `got=len`; writable entries start at `got=0` with cap `len`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_tftp_root_add(
    path: *const c_char,
    data: *mut u8,
    len: u32,
    writable: u32,
) -> i32 {
    if path.is_null() || *path == 0 || data.is_null() || len == 0 {
        return -1;
    }
    let mut slot = None;
    for i in 0..ROOT_MAX {
        if !ROOT[i].used {
            slot = Some(i);
            break;
        }
    }
    let Some(i) = slot else {
        return -1;
    };
    if !coro::copy_cstr(&mut ROOT[i].path, path) {
        return -1;
    }
    ROOT[i].used = true;
    ROOT[i].data = data;
    ROOT[i].cap = len;
    ROOT[i].writable = writable != 0;
    ROOT[i].got = if ROOT[i].writable { 0 } else { len };
    0
}

/// Start a TFTP read of `path` from `host` into `dest`. Await the returned
/// handle; the completion value is 1 on success. 0 = refused.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_tftp_get(
    host: *const c_char,
    path: *const c_char,
    dest: *mut c_void,
    dest_cap: u32,
) -> u32 {
    start_client(MODE_GET, host, path, dest as *mut u8, dest_cap)
}

/// Start a TFTP write of `src` to `path` on `host`. Await the returned handle.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_tftp_put(
    host: *const c_char,
    path: *const c_char,
    src: *const c_void,
    len: u32,
) -> u32 {
    start_client(MODE_PUT, host, path, src as *mut u8, len)
}

/// Bind UDP and serve RRQ/WRQ from the memory root. Returns server handle.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_tftp_listen(port: u16) -> u32 {
    if port == 0 {
        return 0;
    }
    let mut slot = None;
    for i in 0..SRV_MAX {
        if !SERVERS[i].used {
            slot = Some(i);
            break;
        }
    }
    let Some(i) = slot else {
        return 0;
    };
    let h = coro::coro_with_frame::<SrvFrame>(srv_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<SrvFrame>(h);
    (*f).phase = SRV_IDLE;
    (*f).child_h = 0;
    (*f).stop = 0;
    (*f).active = 0;
    (*f).listen_pcb = ip::udp_new_ip_type(ip::IPADDR_TYPE_V4);
    if (*f).listen_pcb.is_null() {
        coro::pm_metal_async_coro_close(h);
        return 0;
    }
    if ip::udp_bind((*f).listen_pcb, &ip::IP_ADDR_ANY, port) != ip::ERR_OK {
        ip::udp_remove((*f).listen_pcb);
        coro::pm_metal_async_coro_close(h);
        return 0;
    }
    (*f).xfer_pcb = core::ptr::null_mut();
    ip::udp_recv((*f).listen_pcb, Some(srv_listen_recv), f as *mut c_void);
    let srv_h = (i + 1) as u32;
    SERVERS[i] = ServerSlot {
        used: true,
        coro_h: h,
    };
    srv_h
}

/// Stop the server coroutine and remove its UDP pcbs.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_tftp_close(srv_h: u32) {
    let s = srv_slot(srv_h);
    if s.is_null() {
        return;
    }
    let f = coro::frame::<SrvFrame>((*s).coro_h);
    if !f.is_null() {
        srv_cleanup_all(f);
        coro::pm_metal_async_coro_close((*s).coro_h);
    }
    (*s).used = false;
    (*s).coro_h = 0;
}

/// `pm_metal_net_tftp_status_t` for `h`, or the last transfer once closed.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_tftp_status(h: u32) -> u32 {
    let f = coro::frame::<TftpFrame>(h);
    if !f.is_null() {
        return (*f).status;
    }
    if LAST_VALID.load(Ordering::Relaxed) {
        LAST_STATUS.load(Ordering::Relaxed)
    } else {
        TftpStatus::PM_METAL_NET_TFTP_BUSY as u32
    }
}

/// Bytes transferred for `h`, or the last transfer once closed.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_tftp_len(h: u32) -> u32 {
    let f = coro::frame::<TftpFrame>(h);
    if !f.is_null() {
        return (*f).got;
    }
    if LAST_VALID.load(Ordering::Relaxed) {
        LAST_LEN.load(Ordering::Relaxed)
    } else {
        0
    }
}
