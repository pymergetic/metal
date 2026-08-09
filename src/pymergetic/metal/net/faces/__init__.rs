//! net.faces — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;


use core::ffi::c_char;

extern "C" {
    fn pm_metal_net_face_mark(bit: u32);
    fn pm_metal_net_face_bits() -> u32;
    fn pm_metal_net_face_format(out: *mut c_char, cap: u32);
}

#[inline] pub fn mark(bit: u32) { unsafe { pm_metal_net_face_mark(bit) } }
#[inline] pub fn bits() -> u32 { unsafe { pm_metal_net_face_bits() } }
#[inline] pub unsafe fn format(out: *mut c_char, cap: u32) { pm_metal_net_face_format(out, cap) }
