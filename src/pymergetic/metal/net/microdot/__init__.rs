//! net.microdot — Rust face over the C into-Py bridge (`bridge.c`).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_microdot_resolve(attr: *const c_char) -> u32;
    fn pm_metal_net_microdot_new() -> u32;
    fn pm_metal_net_microdot_close(h: u32);
    fn pm_metal_net_microdot_version(buf: *mut c_char, buf_len: usize) -> i32;
}

#[inline]
pub unsafe fn resolve(attr: *const c_char) -> u32 {
    pm_metal_net_microdot_resolve(attr)
}
#[inline]
pub fn new() -> u32 {
    unsafe { pm_metal_net_microdot_new() }
}
#[inline]
pub fn close(h: u32) {
    unsafe { pm_metal_net_microdot_close(h) }
}
#[inline]
pub unsafe fn version(buf: *mut c_char, buf_len: usize) -> i32 {
    pm_metal_net_microdot_version(buf, buf_len)
}
