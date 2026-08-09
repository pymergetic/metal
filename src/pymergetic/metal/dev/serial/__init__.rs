//! dev.serial — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;


use core::ffi::c_void;

extern "C" {
    fn pm_metal_dev_serial_write(data: *const u8, n: usize);
    fn pm_metal_dev_serial_console_sink(data: *const u8, n: usize, user: *mut c_void);
}

#[inline] pub unsafe fn write(data: *const u8, n: usize) { pm_metal_dev_serial_write(data, n) }
#[inline] pub unsafe fn console_sink(data: *const u8, n: usize, user: *mut c_void) { pm_metal_dev_serial_console_sink(data, n, user) }
