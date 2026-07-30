//! GENERATED
//! DO NOT HAND-EDIT THIS FILE.
//! This file is:  __init__.rs
//! Edit instead:  __init__.h
//! Regenerate:    metal mod sync
//! Owned by:      metal mod sync (banner = write gate)

#![allow(dead_code, non_camel_case_types)]

extern "C" {
    pub fn pm_metal_util_fourcc_from_u32(out: *mut pm_metal_util_fourcc_t, v: u32);
    pub fn pm_metal_util_fourcc_to_u32(tag: *const pm_metal_util_fourcc_t) -> u32;
    pub fn pm_metal_util_fourcc_from_string(s: *const core::ffi::c_char, out: *mut pm_metal_util_fourcc_t) -> i32;
    pub fn pm_metal_util_fourcc_label(magic: u32, 1]: char out[PM_METAL_UTIL_FOURCC_LEN +) -> i32;
}
