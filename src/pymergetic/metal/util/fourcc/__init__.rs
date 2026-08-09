//! util.fourcc — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;


use core::ffi::c_char;

#[repr(C)]
pub struct pm_metal_util_fourcc_t {
    pub v: u32,
}

extern "C" {
    fn pm_metal_util_fourcc_from_u32(out: *mut pm_metal_util_fourcc_t, v: u32);
    fn pm_metal_util_fourcc_to_u32(tag: *const pm_metal_util_fourcc_t) -> u32;
    fn pm_metal_util_fourcc_from_wire_bytes(bytes: *const u8, out: *mut pm_metal_util_fourcc_t);
    fn pm_metal_util_fourcc_to_wire_bytes(tag: *const pm_metal_util_fourcc_t, out: *mut u8);
    fn pm_metal_util_fourcc_from_bytes(bytes: *const u8, out: *mut pm_metal_util_fourcc_t);
    fn pm_metal_util_fourcc_to_bytes(tag: *const pm_metal_util_fourcc_t, out: *mut u8);
    fn pm_metal_util_fourcc_from_string(s: *const c_char, out: *mut pm_metal_util_fourcc_t) -> i32;
    fn pm_metal_util_fourcc_to_string(tag: *const pm_metal_util_fourcc_t, out: *mut c_char) -> i32;
    fn pm_metal_util_fourcc_label(magic: u32, out: *mut c_char) -> i32;
}

#[inline] pub unsafe fn from_u32(out: *mut pm_metal_util_fourcc_t, v: u32) { pm_metal_util_fourcc_from_u32(out, v) }
#[inline] pub unsafe fn to_u32(tag: *const pm_metal_util_fourcc_t) -> u32 { pm_metal_util_fourcc_to_u32(tag) }
#[inline] pub unsafe fn from_wire_bytes(bytes: *const u8, out: *mut pm_metal_util_fourcc_t) { pm_metal_util_fourcc_from_wire_bytes(bytes, out) }
#[inline] pub unsafe fn to_wire_bytes(tag: *const pm_metal_util_fourcc_t, out: *mut u8) { pm_metal_util_fourcc_to_wire_bytes(tag, out) }
#[inline] pub unsafe fn from_bytes(bytes: *const u8, out: *mut pm_metal_util_fourcc_t) { pm_metal_util_fourcc_from_bytes(bytes, out) }
#[inline] pub unsafe fn to_bytes(tag: *const pm_metal_util_fourcc_t, out: *mut u8) { pm_metal_util_fourcc_to_bytes(tag, out) }
#[inline] pub unsafe fn from_string(s: *const c_char, out: *mut pm_metal_util_fourcc_t) -> i32 { pm_metal_util_fourcc_from_string(s, out) }
#[inline] pub unsafe fn to_string(tag: *const pm_metal_util_fourcc_t, out: *mut c_char) -> i32 { pm_metal_util_fourcc_to_string(tag, out) }
#[inline] pub unsafe fn label(magic: u32, out: *mut c_char) -> i32 { pm_metal_util_fourcc_label(magic, out) }
