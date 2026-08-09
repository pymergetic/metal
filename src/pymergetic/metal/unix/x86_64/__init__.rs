//! unix.x86_64 — Rust face over the C into-Py bridge (`bridge.c`).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_unix_x86_64_name(buf: *mut c_char, buf_len: usize) -> i32;
    fn pm_metal_unix_x86_64_autoexec() -> i32;
}

#[inline]
pub unsafe fn name(buf: *mut c_char, buf_len: usize) -> i32 {
    pm_metal_unix_x86_64_name(buf, buf_len)
}
#[inline]
pub fn autoexec() -> i32 {
    unsafe { pm_metal_unix_x86_64_autoexec() }
}
