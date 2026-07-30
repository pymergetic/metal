//! GENERATED
//! DO NOT HAND-EDIT THIS FILE.
//! This file is:  __init__.rs
//! Edit instead:  __init__.h
//! Source-sha: 7d982bf40e4698fb
//! Regenerate:    metal mod sync
//! Owned by:      metal mod sync (banner = write gate)

#![allow(dead_code, non_camel_case_types)]

#[repr(C)]
#[derive(Clone, Copy)]
pub union pm_metal_util_eightcc_t {
    pub v: u64,
    pub wire: pm_metal_util_eightcc_wire_t,
}

pub type pm_metal_util_eightcc_wire_t = [u8; 8];

extern "C" {
    pub fn pm_metal_util_eightcc_from_u64(out: *mut pm_metal_util_eightcc_t, v: u64);
    pub fn pm_metal_util_eightcc_to_u64(tag: *const pm_metal_util_eightcc_t) -> u64;
    pub fn pm_metal_util_eightcc_from_wire_bytes(bytes: *const u8, out: *mut pm_metal_util_eightcc_t);
    pub fn pm_metal_util_eightcc_to_wire_bytes(tag: *const pm_metal_util_eightcc_t, out: *mut u8);
    pub fn pm_metal_util_eightcc_from_bytes(bytes: *const u8, out: *mut pm_metal_util_eightcc_t);
    pub fn pm_metal_util_eightcc_to_bytes(tag: *const pm_metal_util_eightcc_t, out: *mut u8);
    pub fn pm_metal_util_eightcc_from_string(s: *const core::ffi::c_char, out: *mut pm_metal_util_eightcc_t) -> i32;
    pub fn pm_metal_util_eightcc_to_string(tag: *const pm_metal_util_eightcc_t, out: *mut core::ffi::c_char) -> i32;
    pub fn pm_metal_util_eightcc_label(magic: u64, out: *mut core::ffi::c_char) -> i32;
}
