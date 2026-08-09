//! net.tftp — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_tftp_get(server_ip: u32, filename: *const c_char, buf: *mut u8, cap: u32, len_out: *mut u32) -> i32;
}

#[inline] pub unsafe fn get(server_ip: u32, filename: *const c_char, buf: *mut u8, cap: u32, len_out: *mut u32) -> i32 { pm_metal_net_tftp_get(server_ip, filename, buf, cap, len_out) }
