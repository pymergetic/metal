//! dev.blk — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_dev_blk_detect() -> i32;
    fn pm_metal_dev_blk_open() -> i32;
    fn pm_metal_dev_blk_capacity_sectors() -> u64;
    fn pm_metal_dev_blk_read(lba: u64, buf: *mut c_void, nsec: u32) -> i32;
    fn pm_metal_dev_blk_read_async(lba: u64, buf: *mut c_void, nsec: u32) -> u32;
    fn pm_metal_dev_blk_result(h: u32) -> u32;
}

#[inline] pub fn detect() -> i32 { unsafe { pm_metal_dev_blk_detect() } }
#[inline] pub fn open() -> i32 { unsafe { pm_metal_dev_blk_open() } }
#[inline] pub fn capacity_sectors() -> u64 { unsafe { pm_metal_dev_blk_capacity_sectors() } }
#[inline] pub unsafe fn read(lba: u64, buf: *mut c_void, nsec: u32) -> i32 { pm_metal_dev_blk_read(lba, buf, nsec) }
#[inline] pub unsafe fn read_async(lba: u64, buf: *mut c_void, nsec: u32) -> u32 { pm_metal_dev_blk_read_async(lba, buf, nsec) }
#[inline] pub fn result(h: u32) -> u32 { unsafe { pm_metal_dev_blk_result(h) } }
