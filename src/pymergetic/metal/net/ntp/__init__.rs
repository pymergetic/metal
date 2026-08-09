//! net.ntp — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_ntp_sync(server_ip: u32) -> u32;
    fn pm_metal_net_ntp_sync_host(host: *const c_char) -> u32;
    fn pm_metal_net_ntp_poll();
    fn pm_metal_net_ntp_status() -> i32;
    fn pm_metal_net_ntp_last_unix_secs() -> u32;
    fn pm_metal_net_ntp_query(server_ip: u32, unix_secs_out: *mut u32) -> i32;
    fn pm_metal_net_ntp_query_host(host: *const c_char, unix_secs_out: *mut u32) -> i32;
}

#[inline] pub fn sync(server_ip: u32) -> u32 { unsafe { pm_metal_net_ntp_sync(server_ip) } }
#[inline] pub unsafe fn sync_host(host: *const c_char) -> u32 { pm_metal_net_ntp_sync_host(host) }
#[inline] pub fn poll() { unsafe { pm_metal_net_ntp_poll() } }
#[inline] pub fn status() -> i32 { unsafe { pm_metal_net_ntp_status() } }
#[inline] pub fn last_unix_secs() -> u32 { unsafe { pm_metal_net_ntp_last_unix_secs() } }
#[inline] pub unsafe fn query(server_ip: u32, unix_secs_out: *mut u32) -> i32 { pm_metal_net_ntp_query(server_ip, unix_secs_out) }
#[inline] pub unsafe fn query_host(host: *const c_char, unix_secs_out: *mut u32) -> i32 { pm_metal_net_ntp_query_host(host, unix_secs_out) }
