//! GENERATED
//! DO NOT HAND-EDIT THIS FILE.
//! This file is:  __init__.rs
//! Edit instead:  __init__.h
//! Source-sha: 98e754646bb99093
//! Regenerate:    metal mod sync
//! Owned by:      metal mod sync (banner = write gate)

#![allow(dead_code, non_camel_case_types)]

#[repr(C)]
#[derive(Clone, Copy)]
pub union pm_metal_util_fourcc_t {
    pub v: u32,
    pub wire: pm_metal_util_fourcc_wire_t,
}

pub type pm_metal_util_fourcc_wire_t = [u8; 4];

extern "C" {
    pub fn pm_metal_util_fourcc_from_u32(out: *mut pm_metal_util_fourcc_t, v: u32);
    pub fn pm_metal_util_fourcc_to_u32(tag: *const pm_metal_util_fourcc_t) -> u32;
    pub fn pm_metal_util_fourcc_from_wire_bytes(bytes: *const u8, out: *mut pm_metal_util_fourcc_t);
    pub fn pm_metal_util_fourcc_to_wire_bytes(tag: *const pm_metal_util_fourcc_t, out: *mut u8);
    pub fn pm_metal_util_fourcc_from_bytes(bytes: *const u8, out: *mut pm_metal_util_fourcc_t);
    pub fn pm_metal_util_fourcc_to_bytes(tag: *const pm_metal_util_fourcc_t, out: *mut u8);
    pub fn pm_metal_util_fourcc_from_string(s: *const core::ffi::c_char, out: *mut pm_metal_util_fourcc_t) -> i32;
    pub fn pm_metal_util_fourcc_to_string(tag: *const pm_metal_util_fourcc_t, out: *mut core::ffi::c_char) -> i32;
    pub fn pm_metal_util_fourcc_label(magic: u32, out: *mut core::ffi::c_char) -> i32;
}
