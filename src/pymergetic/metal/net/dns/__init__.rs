//! DNS resolve awaitable on net.ip (lwIP dns + Metal coro).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use core::ffi::{c_char, c_void};
use core::ptr;

use pymergetic_metal_async as _;
use pymergetic_metal_net_ip as ip;
use pymergetic_metal_net_ip::coro::{
    self, pm_metal_async_await, pm_metal_async_coro_close, pm_metal_async_coro_create,
    pm_metal_async_coro_state, pm_metal_async_set_result_u32, pm_metal_async_sleep_us,
    pm_metal_time_mono_us, ERROR, WAITING,
};
use pymergetic_metal_rt as _;

const DNS_QUERY_SLOTS: usize = 4;
const DNS_DEADLINE_US: u64 = 8_000_000;

struct DnsQuery {
    used: bool,
    done: bool,
    ok: bool,
    abandoned: bool,
    addr: ip::ip_addr_t,
}

#[repr(C)]
struct DnsCoro {
    query: *mut DnsQuery,
    sleep_h: u32,
    deadline: u64,
}

static mut QUERIES: [DnsQuery; DNS_QUERY_SLOTS] = unsafe { core::mem::zeroed() };
static mut DNS_LAST: ip::ip4_addr_t = ip::ip4_addr_t { addr: 0 };
static mut DNS_LAST_VALID: bool = false;

unsafe fn query_alloc() -> *mut DnsQuery {
    for i in 0..DNS_QUERY_SLOTS {
        if !QUERIES[i].used {
            QUERIES[i].used = true;
            QUERIES[i].done = false;
            QUERIES[i].ok = false;
            QUERIES[i].abandoned = false;
            QUERIES[i].addr = ip::ip_addr_t { addr: 0 };
            return &mut QUERIES[i];
        }
    }
    ptr::null_mut()
}

unsafe extern "C" fn dns_found(
    _name: *const c_char,
    addr: *const ip::ip_addr_t,
    arg: *mut c_void,
) {
    let query = arg as *mut DnsQuery;
    if query.is_null() || !(*query).used {
        return;
    }
    (*query).done = true;
    (*query).ok = !addr.is_null();
    if (*query).ok {
        (*query).addr = *addr;
    }
    if (*query).abandoned {
        (*query).used = false;
    }
}

unsafe fn dns_start_sleep(self_h: u32, coro: *mut DnsCoro) -> u32 {
    (*coro).sleep_h = pm_metal_async_sleep_us(2000);
    if (*coro).sleep_h == 0 {
        return ERROR;
    }
    pm_metal_async_await(self_h, (*coro).sleep_h)
}

unsafe extern "C" fn dns_step(self_h: u32) -> u32 {
    let coro = pm_metal_async_coro_state(self_h) as *mut DnsCoro;
    if coro.is_null() || (*coro).query.is_null() {
        return ERROR;
    }

    if (*coro).sleep_h != 0 {
        let status = pm_metal_async_await(self_h, (*coro).sleep_h);
        if status == WAITING {
            return WAITING;
        }
        pm_metal_async_coro_close((*coro).sleep_h);
        (*coro).sleep_h = 0;
        if status != coro::DONE {
            return ERROR;
        }
    }

    ip::pm_metal_net_ip_poll();
    let query = (*coro).query;
    if (*query).done {
        if (*query).ok {
            DNS_LAST = (*query).addr;
            DNS_LAST_VALID = true;
            (*query).used = false;
            pm_metal_async_set_result_u32(self_h, 1);
            return coro::DONE;
        }
        (*query).used = false;
        pm_metal_async_set_result_u32(self_h, 0);
        return ERROR;
    }

    if pm_metal_time_mono_us() >= (*coro).deadline {
        (*query).abandoned = true;
        if (*query).done {
            (*query).used = false;
        }
        pm_metal_async_set_result_u32(self_h, 0);
        return ERROR;
    }

    dns_start_sleep(self_h, coro)
}

/// After a successful resolve (`result_u32 == 1`), last address as `ip_addr_t`.
pub unsafe fn last_addr() -> Option<ip::ip_addr_t> {
    let mut text = [0u8; 16];
    if pm_metal_net_dns_last_ntoa(text.as_mut_ptr() as *mut c_char, text.len() as u32) != 0 {
        return None;
    }
    ip::aton(text.as_ptr() as *const c_char)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_dns(host: *const c_char) -> u32 {
    if host.is_null() || *host == 0 {
        return 0;
    }
    let query = query_alloc();
    if query.is_null() {
        return 0;
    }

    let h = pm_metal_async_coro_create(Some(dns_step), core::mem::size_of::<DnsCoro>() as u32);
    if h == 0 {
        (*query).used = false;
        return 0;
    }
    let coro = pm_metal_async_coro_state(h) as *mut DnsCoro;
    if coro.is_null() {
        (*query).used = false;
        pm_metal_async_coro_close(h);
        return 0;
    }

    (*coro).query = query;
    (*coro).sleep_h = 0;
    (*coro).deadline = pm_metal_time_mono_us() + DNS_DEADLINE_US;
    DNS_LAST_VALID = false;

    let mut literal = ip::ip4_addr_t::default();
    if ip::ip4addr_aton(host, &mut literal) != 0 {
        (*query).addr = literal;
        (*query).done = true;
        (*query).ok = true;
        return h;
    }

    let mut addr = ip::ip_addr_t::default();
    let err = ip::dns_gethostbyname_addrtype(
        host,
        &mut addr,
        Some(dns_found),
        query as *mut c_void,
        ip::LWIP_DNS_ADDRTYPE_IPV4,
    );
    if err == ip::ERR_OK {
        (*query).addr = addr;
        (*query).done = true;
        (*query).ok = true;
    } else if err != ip::ERR_INPROGRESS {
        (*query).used = false;
        pm_metal_async_coro_close(h);
        return 0;
    }
    h
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_dns_last_ntoa(out: *mut c_char, out_cap: u32) -> i32 {
    if out.is_null() || out_cap == 0 || !DNS_LAST_VALID {
        return -1;
    }
    if ip::ip4addr_ntoa_r(core::ptr::addr_of!(DNS_LAST), out, out_cap as i32).is_null() {
        -1
    } else {
        0
    }
}
