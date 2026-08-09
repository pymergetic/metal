//! net.dns — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_dns_lookup(name: *const c_char) -> u32;
    fn pm_metal_net_dns_last_addr() -> u32;
    fn pm_metal_net_dns_resolve(name: *const c_char, addr_out: *mut u32) -> i32;
}

#[inline] pub unsafe fn lookup(name: *const c_char) -> u32 { pm_metal_net_dns_lookup(name) }
#[inline] pub fn last_addr() -> u32 { unsafe { pm_metal_net_dns_last_addr() } }
#[inline] pub unsafe fn resolve(name: *const c_char, addr_out: *mut u32) -> i32 { pm_metal_net_dns_resolve(name, addr_out) }
