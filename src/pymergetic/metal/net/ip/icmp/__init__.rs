//! ICMP echo (ping) awaitable on net.ip.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

use pymergetic_metal_async as _;
use pymergetic_metal_net_dns as dns;
use pymergetic_metal_net_ip as ip;
use pymergetic_metal_net_ip::coro::{self, Child};
use pymergetic_metal_rt as _;

const PING_HOST_MAX: usize = 128;
const PING_ID: u16 = 0x4d54;
const PING_DATA_SIZE: u16 = 32;
const PING_SEND_TRIES: u32 = 40;

const PHASE_RESOLVE: u32 = 0;
const PHASE_DNS_WAIT: u32 = 1;
const PHASE_OPEN: u32 = 2;
const PHASE_SEND: u32 = 3;
const PHASE_SEND_SLEEP: u32 = 4;
const PHASE_REPLY_WAIT: u32 = 5;

/// Why the last `pm_metal_net_ip_icmp_ping` ended (see `pm_metal_net_ip_icmp_ping_last_err`).
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_net_ip_icmp_ping_err_t {
    PM_METAL_NET_IP_ICMP_PING_ERR_NONE = 0,
    PM_METAL_NET_IP_ICMP_PING_ERR_RESOLVE = 1,
    PM_METAL_NET_IP_ICMP_PING_ERR_SEND = 2,
    PM_METAL_NET_IP_ICMP_PING_ERR_TIMEOUT = 3,
    PM_METAL_NET_IP_ICMP_PING_ERR_NOROUTE = 4,
    PM_METAL_NET_IP_ICMP_PING_ERR_NOMEM = 5,
}

use pm_metal_net_ip_icmp_ping_err_t as PingErr;

#[repr(C)]
struct PingFrame {
    phase: u32,
    host: [u8; PING_HOST_MAX],
    timeout_ms: u32,
    child_h: u32,
    send_tries: u32,
    pcb: *mut ip::raw_pcb,
    target: ip::ip_addr_t,
    seq: u16,
    reply: u32,
    rtt_us: u32,
    send_us: u64,
    deadline: u64,
}

/// Last completed ping, for callers that already closed the handle.
static LAST_VALID: AtomicBool = AtomicBool::new(false);
static LAST_RTT_US: AtomicU32 = AtomicU32::new(0);
static LAST_ERR: AtomicU32 =
    AtomicU32::new(PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_NONE as u32);

unsafe fn cleanup(f: *mut PingFrame) {
    if !(*f).pcb.is_null() {
        ip::raw_remove((*f).pcb);
        (*f).pcb = core::ptr::null_mut();
    }
}

unsafe fn fail(f: *mut PingFrame, err: PingErr) -> u32 {
    cleanup(f);
    LAST_ERR.store(err as u32, Ordering::Relaxed);
    coro::ERROR
}

unsafe extern "C" fn ping_recv(
    arg: *mut c_void,
    _pcb: *mut ip::raw_pcb,
    p: *mut ip::pbuf,
    _addr: *const ip::ip_addr_t,
) -> u8 {
    let f = arg as *mut PingFrame;
    if f.is_null() || p.is_null() || (*f).reply != 0 {
        return 0;
    }

    /* raw_input hands the callback the packet with payload at the IP header. */
    let hlen = ip::iph_hl_bytes((*p).payload as *const ip::ip_hdr);
    let echo_len = core::mem::size_of::<ip::icmp_echo_hdr>() as u16;
    if (*p).tot_len < hlen + echo_len || ip::pbuf_remove_header(p, hlen as usize) != 0 {
        return 0;
    }

    let echo = (*p).payload as *const ip::icmp_echo_hdr;
    if (*echo).type_ != ip::ICMP_ER
        || (*echo).id != PING_ID
        || (*echo).seqno != ip::lwip_htons((*f).seq)
    {
        ip::pbuf_add_header(p, hlen as usize);
        return 0;
    }

    let elapsed = coro::pm_metal_time_mono_us().wrapping_sub((*f).send_us);
    (*f).rtt_us = if elapsed > u32::MAX as u64 {
        u32::MAX
    } else {
        elapsed as u32
    };
    (*f).reply = 1;
    ip::pbuf_free(p);
    1
}

unsafe fn send_echo(f: *mut PingFrame) -> ip::err_t {
    let echo_len = core::mem::size_of::<ip::icmp_echo_hdr>() as u16;
    let len = echo_len + PING_DATA_SIZE;
    let p = ip::pbuf_alloc(ip::PBUF_IP, len, ip::PBUF_RAM);
    if p.is_null() {
        return ip::ERR_MEM;
    }

    let echo = (*p).payload as *mut ip::icmp_echo_hdr;
    (*echo).type_ = ip::ICMP_ECHO;
    (*echo).code = 0;
    (*echo).chksum = 0;
    (*echo).id = PING_ID;
    (*echo).seqno = ip::lwip_htons((*f).seq);
    let data = (echo as *mut u8).add(echo_len as usize);
    for i in 0..PING_DATA_SIZE {
        *data.add(i as usize) = i as u8;
    }
    (*echo).chksum = ip::inet_chksum(echo as *const c_void, len);

    (*f).send_us = coro::pm_metal_time_mono_us();
    let err = ip::raw_sendto((*f).pcb, p, &(*f).target);
    ip::pbuf_free(p);
    err
}

unsafe fn start_sleep(self_h: u32, f: *mut PingFrame, us: u64) -> u32 {
    match coro::start_sleep(self_h, &mut (*f).child_h, us) {
        Some(status) => status,
        None => fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_SEND),
    }
}

unsafe extern "C" fn ping_step(self_h: u32) -> u32 {
    let f = coro::frame::<PingFrame>(self_h);
    if f.is_null() {
        return coro::ERROR;
    }

    match (*f).phase {
        PHASE_RESOLVE => {
            if let Some(literal) = ip::aton((*f).host.as_ptr() as *const c_char) {
                (*f).target = literal;
                (*f).phase = PHASE_OPEN;
                return coro::PENDING;
            }
            (*f).child_h = dns::pm_metal_net_dns((*f).host.as_ptr() as *const c_char);
            if (*f).child_h == 0 {
                return fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_RESOLVE);
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
                return fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_RESOLVE);
            };
            (*f).target = addr;
            (*f).phase = PHASE_OPEN;
            coro::PENDING
        }

        PHASE_OPEN => {
            (*f).pcb = ip::raw_new(ip::IP_PROTO_ICMP);
            if (*f).pcb.is_null() {
                return fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_NOMEM);
            }
            ip::raw_recv((*f).pcb, Some(ping_recv), f as *mut c_void);
            (*f).seq = ((coro::pm_metal_time_mono_us() >> 10) & 0xffff) as u16;
            (*f).phase = PHASE_SEND;
            coro::PENDING
        }

        PHASE_SEND => {
            coro::pm_metal_net_ip_poll();
            let err = send_echo(f);
            if err == ip::ERR_OK {
                (*f).deadline = coro::pm_metal_time_mono_us() + (*f).timeout_ms as u64 * 1000;
                (*f).phase = PHASE_REPLY_WAIT;
                return coro::PENDING;
            }
            let retryable = matches!(
                err,
                ip::ERR_RTE | ip::ERR_IF | ip::ERR_MEM | ip::ERR_BUF
            );
            if retryable && (*f).send_tries < PING_SEND_TRIES {
                (*f).send_tries += 1;
                (*f).phase = PHASE_SEND_SLEEP;
                return start_sleep(self_h, f, 25000);
            }
            match err {
                ip::ERR_RTE => fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_NOROUTE),
                ip::ERR_MEM | ip::ERR_BUF => fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_NOMEM),
                _ => fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_SEND),
            }
        }

        PHASE_SEND_SLEEP => match coro::finish_child(self_h, &mut (*f).child_h) {
            Child::Waiting => coro::WAITING,
            Child::Failed => fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_SEND),
            Child::Done(_) => {
                (*f).phase = PHASE_SEND;
                coro::PENDING
            }
        },

        PHASE_REPLY_WAIT => {
            if (*f).child_h != 0 {
                match coro::finish_child(self_h, &mut (*f).child_h) {
                    Child::Waiting => return coro::WAITING,
                    Child::Failed => return fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_SEND),
                    Child::Done(_) => {}
                }
            }
            coro::pm_metal_net_ip_poll();
            if (*f).reply != 0 {
                cleanup(f);
                LAST_VALID.store(true, Ordering::Relaxed);
                LAST_RTT_US.store((*f).rtt_us, Ordering::Relaxed);
                LAST_ERR.store(
                    PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_NONE as u32,
                    Ordering::Relaxed,
                );
                coro::pm_metal_async_set_result_u32(self_h, (*f).rtt_us / 1000);
                return coro::DONE;
            }
            if coro::pm_metal_time_mono_us() >= (*f).deadline {
                return fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_TIMEOUT);
            }
            start_sleep(self_h, f, 2000)
        }

        _ => fail(f, PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_SEND),
    }
}

/// Start an ICMP echo to `host` (hostname or dotted quad). Await the returned
/// handle; the completion value is the round trip in milliseconds. 0 = refused.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_icmp_ping(host: *const c_char, timeout_ms: u32) -> u32 {
    if host.is_null() || *host == 0 || timeout_ms == 0 {
        return 0;
    }
    let h = coro::coro_with_frame::<PingFrame>(ping_step);
    if h == 0 {
        return 0;
    }
    let f = coro::frame::<PingFrame>(h);
    if !coro::copy_cstr(&mut (*f).host, host) {
        coro::pm_metal_async_coro_close(h);
        return 0;
    }
    (*f).phase = PHASE_RESOLVE;
    (*f).timeout_ms = timeout_ms;
    LAST_VALID.store(false, Ordering::Relaxed);
    LAST_ERR.store(
        PingErr::PM_METAL_NET_IP_ICMP_PING_ERR_NONE as u32,
        Ordering::Relaxed,
    );
    h
}

/// Round trip in milliseconds for `h`, or the last completed ping once closed.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_icmp_ping_rtt_ms(h: u32) -> u32 {
    pm_metal_net_ip_icmp_ping_rtt_us(h) / 1000
}

/// Round trip in microseconds for `h`, or the last completed ping once closed.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_icmp_ping_rtt_us(h: u32) -> u32 {
    let f = coro::frame::<PingFrame>(h);
    if !f.is_null() {
        return (*f).rtt_us;
    }
    if LAST_VALID.load(Ordering::Relaxed) {
        LAST_RTT_US.load(Ordering::Relaxed)
    } else {
        0
    }
}

/// `pm_metal_net_ip_icmp_ping_err_t` for the most recent ping.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_icmp_ping_last_err() -> u32 {
    LAST_ERR.load(Ordering::Relaxed)
}
