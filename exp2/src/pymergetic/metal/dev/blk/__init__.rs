//! GENERATED
//! DO NOT HAND-EDIT THIS FILE.
//! This file is:  __init__.rs
//! Edit instead:  __init__.h
//! Regenerate:    metal mod sync
//! Owned by:      metal mod sync (banner = write gate)

#![allow(dead_code, non_camel_case_types)]

extern "C" {
    pub fn pm_metal_dev_blk_detect() -> i32;
    pub fn pm_metal_dev_blk_open() -> i32;
    pub fn pm_metal_dev_blk_capacity_sectors() -> u64;
    pub fn pm_metal_dev_blk_read(lba: u64, buf: *mut core::ffi::c_void, nsec: u32) -> i32;
    pub fn pm_metal_dev_blk_read_async(lba: u64, buf: *mut core::ffi::c_void, nsec: u32) -> u32;
    pub fn pm_metal_dev_blk_result(h: u32) -> u32;
}
