//! IPv4 + DHCP over pluggable L2 (lwIP NO_SYS).
//!
//! Pure Rust Metal border + bring-up. Vanilla lwIP stays in `external/lwip`
//! (FFI in [`lwip`]). Nested: [`ssl`] (creds/CA); siblings
//! `net/ip/{tcp,udp,icmp}` (`tcp` = opaque byte stream, clear or SSL).
//! Apps (dns/http/ftp/tftp/ntp) sit under `net/`.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use core::ffi::{c_char, c_void};

use pymergetic_metal_async as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_rt as _;

#[path = "_lwip.rs"]
mod lwip;

pub use lwip::*;

#[path = "_coro.rs"]
pub mod coro;

#[path = "_stack.rs"]
mod stack;

#[path = "ssl/__init__.rs"]
pub mod ssl;

/* --- L2 ops (pool types; named aliases so sync emits C fn-ptr typedefs) --- */

pub type pm_metal_net_ip_l2_rx_fn =
    Option<unsafe extern "C" fn(ctx: *mut c_void, frame: *const u8, len: u32)>;

pub type pm_metal_net_ip_l2_poll_fn =
    Option<unsafe extern "C" fn(rx: pm_metal_net_ip_l2_rx_fn, ctx: *mut c_void)>;

pub type pm_metal_net_ip_l2_open_fn =
    Option<unsafe extern "C" fn(mac_out: *mut u8) -> i32>;

pub type pm_metal_net_ip_l2_mac_fn = Option<unsafe extern "C" fn() -> *const u8>;

pub type pm_metal_net_ip_l2_tx_fn =
    Option<unsafe extern "C" fn(frame: *const c_void, len: u32) -> i32>;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_metal_net_ip_l2_ops_t {
    pub open: pm_metal_net_ip_l2_open_fn,
    pub mac: pm_metal_net_ip_l2_mac_fn,
    pub tx: pm_metal_net_ip_l2_tx_fn,
    pub poll: pm_metal_net_ip_l2_poll_fn,
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_poll() {
    stack::poll()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_if_count() -> u32 {
    stack::if_count()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_if_status_index(
    index: u32,
    dest: *mut c_char,
    dest_cap: u32,
) -> i32 {
    stack::if_status_index(index, dest, dest_cap)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_l2_start(
    backend: *const c_char,
    ops: *const pm_metal_net_ip_l2_ops_t,
) -> i32 {
    stack::l2_start(backend, ops)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_loopback_start() -> i32 {
    stack::loopback_start()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_if_dhcp_ready(
    ifname: *const c_char,
    ip_out: *mut c_char,
    ip_cap: u32,
) -> i32 {
    stack::if_dhcp_ready(ifname, ip_out, ip_cap)
}
