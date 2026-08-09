//! net.ntp — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_ntp_query(server_ip: u32, unix_secs_out: *mut u32) -> i32;
    fn pm_metal_net_ntp_query_host(host: *const c_char, unix_secs_out: *mut u32) -> i32;
}

#[inline] pub unsafe fn query(server_ip: u32, unix_secs_out: *mut u32) -> i32 { pm_metal_net_ntp_query(server_ip, unix_secs_out) }
#[inline] pub unsafe fn query_host(host: *const c_char, unix_secs_out: *mut u32) -> i32 { pm_metal_net_ntp_query_host(host, unix_secs_out) }
