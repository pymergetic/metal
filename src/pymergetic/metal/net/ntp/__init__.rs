//! SNTP client awaitable on net.ip (lwIP UDP, unicast client mode).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};
use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

use pymergetic_metal_async as _;
use pymergetic_metal_net_dns as dns;
use pymergetic_metal_net_ip as ip;
use pymergetic_metal_net_ip::coro::{self, Child};
use pymergetic_metal_rt as _;

const NTP_HOST_MAX: usize = 128;
const NTP_PACKET_LEN: u16 = 48;
const NTP_TIMEOUT_US: u64 = 3_000_000;
const NTP_RETRIES: u32 = 3;
/// Seconds between the NTP epoch (1900) and the Unix epoch (1970).
const NTP_UNIX_OFFSET: u64 = 2_208_988_800;

/// `forge build --stress` runs a host NTP mock on an unprivileged port.
#[cfg(feature = "exp2_stress")]
const NTP_PORT: u16 = 18123;
#[cfg(not(feature = "exp2_stress"))]
const NTP_PORT: u16 = 123;

const PHASE_RESOLVE: u32 = 0;
const PHASE_DNS_WAIT: u32 = 1;
const PHASE_OPEN: u32 = 2;
const PHASE_SEND: u32 = 3;
const PHASE_REPLY_WAIT: u32 = 4;

/// `pm_metal_net_ntp_status` codes.
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_net_ntp_status_t {
    PM_METAL_NET_NTP_OK = 0,
    PM_METAL_NET_NTP_BUSY = 1,
    PM_METAL_NET_NTP_ERR_RESOLVE = 3,
    PM_METAL_NET_NTP_ERR_SOCKET = 4,
    PM_METAL_NET_NTP_ERR_SEND = 5,
    PM_METAL_NET_NTP_ERR_REPLY = 6,
    PM_METAL_NET_NTP_ERR_TIMEOUT = 7,
}

use pm_metal_net_ntp_status_t as NtpStatus;

#[repr(C)]
struct NtpFrame {
    phase: u32,
    host: [u8; NTP_HOST_MAX],
    child_h: u32,
    status: u32,
    retries: u32,
    received: u32,
    deadline: u64,
    unix_ms: u64,
    server: ip::ip_addr_t,
    pcb: *mut ip::udp_pcb,
    packet: [u8; NTP_PACKET_LEN as usize],
}

/// Last completed sync, for callers that already closed the handle.
static LAST_VALID: AtomicBool = AtomicBool::new(false);
static LAST_STATUS: AtomicU32 = AtomicU32::new(NtpStatus::PM_METAL_NET_NTP_BUSY as u32);
static LAST_UNIX_MS: AtomicU64 = AtomicU64::new(0);

unsafe fn cleanup(f: *mut NtpFrame) {
    if !(*f).pcb.is_null() {
        ip::udp_remove((*f).pcb);
        (*f).pcb = core::ptr::null_mut();
    }
}

unsafe fn finish(f: *mut NtpFrame, self_h: u32, status: NtpStatus) -> u32 {
    cleanup(f);
    let code = status as u32;
    (*f).status = code;
    LAST_VALID.store(true, Ordering::Relaxed);
    LAST_STATUS.store(code, Ordering::Relaxed);
    LAST_UNIX_MS.store((*f).unix_ms, Ordering::Relaxed);
    let ok = status == NtpStatus::PM_METAL_NET_NTP_OK;
    coro::pm_metal_async_set_result_u32(self_h, ok as u32);
    if ok {
        coro::DONE
    } else {
        coro::ERROR
    }
}

unsafe extern "C" fn ntp_recv(
    arg: *mut c_void,
    _pcb: *mut ip::udp_pcb,
    p: *mut ip::pbuf,
    addr: *const ip::ip_addr_t,
    port: u16,
) {
    let f = arg as *mut NtpFrame;
    if f.is_null() || p.is_null() {
        if !p.is_null() {
            ip::pbuf_free(p);
        }
        return;
    }
    if (*f).received == 0
        && port == NTP_PORT
        && ip::ip_addr_eq(addr, &(*f).server)
        && (*p).tot_len >= NTP_PACKET_LEN
        && ip::pbuf_copy_partial(
            p,
            (*f).packet.as_mut_ptr() as *mut c_void,
            NTP_PACKET_LEN,
            0,
        ) == NTP_PACKET_LEN
    {
        (*f).received = 1;
    }
    ip::pbuf_free(p);
}

fn read_be32(b: &[u8]) -> u32 {
    ((b[0] as u32) << 24) | ((b[1] as u32) << 16) | ((b[2] as u32) << 8) | (b[3] as u32)
}

/// Validate the reply (LI != 3, stratum != 0, server/broadcast mode) and turn
/// its transmit timestamp into Unix milliseconds.
unsafe fn parse(f: *mut NtpFrame) -> bool {
    let packet = (*f).packet;
    let mode = packet[0] & 7;
    if (packet[0] >> 6) == 3 || packet[1] == 0 || (mode != 4 && mode != 5) {
        return false;
    }
    let sec = read_be32(&packet[40..44]) as u64;
    let frac = read_be32(&packet[44..48]) as u64;
    if sec < NTP_UNIX_OFFSET {
        return false;
    }
    (*f).unix_ms = (sec - NTP_UNIX_OFFSET) * 1000 + (frac * 1000) / 0x1_0000_0000;
    true
}

unsafe fn send_request(f: *mut NtpFrame) -> bool {
    let p = ip::pbuf_alloc(ip::PBUF_TRANSPORT, NTP_PACKET_LEN, ip::PBUF_RAM);
    if p.is_null() {
        return false;
    }
    let packet = (*p).payload as *mut u8;
    for i in 0..NTP_PACKET_LEN as usize {
        *packet.add(i) = 0;
    }
    /* LI=0, VN=4, Mode=3 (client). */
    *packet = 0x23;
    let err = ip::udp_sendto((*f).pcb, p, &(*f).server, NTP_PORT);
    ip::pbuf_free(p);
    err == ip::ERR_OK
}

unsafe extern "C" fn ntp_step(self_h: u32) -> u32 {
    let f = coro::frame::<NtpFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }

    match (*f).phase {
        PHASE_RESOLVE => {
            if let Some(literal) = ip::aton((*f).host.as_ptr() as *const c_char) {
                (*f).server = literal;
                (*f).phase = PHASE_OPEN;
                return coro::PENDING;
            }
            (*f).child_h = dns::pm_metal_net_dns((*f).host.as_ptr() as *const c_char);
            if (*f).child_h == 0 {
                return finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_RESOLVE);
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
                return finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_RESOLVE);
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
                return finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_SOCKET);
            }
            ip::udp_recv((*f).pcb, Some(ntp_recv), f as *mut c_void);
            (*f).phase = PHASE_SEND;
            coro::PENDING
        }

        PHASE_SEND => {
            (*f).received = 0;
            if !send_request(f) {
                return finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_SEND);
            }
            (*f).deadline = coro::pm_metal_time_mono_us() + NTP_TIMEOUT_US;
            (*f).phase = PHASE_REPLY_WAIT;
            coro::PENDING
        }

        PHASE_REPLY_WAIT => {
            if (*f).child_h != 0 {
                match coro::finish_child(self_h, &mut (*f).child_h) {
                    Child::Waiting => return coro::WAITING,
                    Child::Failed => {
                        return finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_SOCKET)
                    }
                    Child::Done(_) => {}
                }
            }
            coro::pm_metal_net_ip_poll();
            if (*f).received != 0 {
                if !parse(f) {
                    return finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_REPLY);
                }
                return finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_OK);
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                (*f).retries += 1;
                if (*f).retries > NTP_RETRIES {
                    return finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_TIMEOUT);
                }
                (*f).phase = PHASE_SEND;
                return coro::PENDING;
            }
            match coro::start_sleep(self_h, &mut (*f).child_h, 2000) {
                Some(status) => status,
                None => finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_SOCKET),
            }
        }

        _ => finish(f, self_h, NtpStatus::PM_METAL_NET_NTP_ERR_SOCKET),
    }
}

/// Start an SNTP query against `host` (hostname or dotted quad). Await the
/// returned handle; the completion value is 1 on success. 0 = refused.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ntp_sync(host: *const c_char) -> u32 {
    if host.is_null() || *host == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<NtpFrame>(ntp_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<NtpFrame>(h);
    if !coro::copy_cstr(&mut (*f).host, host) {
        coro::pm_metal_async_coro_close(h);
        return 0;
    }
    (*f).phase = PHASE_RESOLVE;
    (*f).status = NtpStatus::PM_METAL_NET_NTP_BUSY as u32;
    LAST_VALID.store(false, Ordering::Relaxed);
    h
}

/// `pm_metal_net_ntp_status_t` for `h`, or the last sync once closed.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ntp_status(h: u32) -> u32 {
    let f = coro::frame::<NtpFrame>(h);
    if !f.is_null() {
        return (*f).status;
    }
    if LAST_VALID.load(Ordering::Relaxed) {
        LAST_STATUS.load(Ordering::Relaxed)
    } else {
        NtpStatus::PM_METAL_NET_NTP_BUSY as u32
    }
}

/// Unix milliseconds from the last successful sync, else 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ntp_last_unix_ms() -> u64 {
    if LAST_VALID.load(Ordering::Relaxed)
        && LAST_STATUS.load(Ordering::Relaxed) == NtpStatus::PM_METAL_NET_NTP_OK as u32
    {
        LAST_UNIX_MS.load(Ordering::Relaxed)
    } else {
        0
    }
}
