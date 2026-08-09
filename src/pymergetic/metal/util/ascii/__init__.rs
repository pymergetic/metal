//! util.ascii — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;


use core::ffi::c_char;

extern "C" {
    fn pm_metal_util_ascii_bound(text_len: usize) -> usize;
    fn pm_metal_util_ascii_render(text: *const c_char, ink: c_char, out: *mut c_char, out_cap: usize) -> i32;
    fn pm_metal_util_ascii_log(text: *const c_char);
    fn pm_metal_util_ascii_log_cyan(text: *const c_char);
    fn pm_metal_util_ascii_log_rainbow(text: *const c_char);
}

#[inline] pub fn bound(text_len: usize) -> usize { unsafe { pm_metal_util_ascii_bound(text_len) } }
#[inline] pub unsafe fn render(text: *const c_char, ink: c_char, out: *mut c_char, out_cap: usize) -> i32 { pm_metal_util_ascii_render(text, ink, out, out_cap) }
#[inline] pub unsafe fn log(text: *const c_char) { pm_metal_util_ascii_log(text) }
#[inline] pub unsafe fn log_cyan(text: *const c_char) { pm_metal_util_ascii_log_cyan(text) }
#[inline] pub unsafe fn log_rainbow(text: *const c_char) { pm_metal_util_ascii_log_rainbow(text) }
