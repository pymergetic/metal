//! boot — Rust face over boot.tree C helpers.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_boot_banner(version: *const u8, cpu: *const u8);
    fn pm_metal_boot_emit(line: *const u8);
    fn pm_metal_boot_tree_ready_ok();
    fn pm_metal_boot_tree_print() -> i32;
}

#[inline]
pub unsafe fn banner(version: *const u8, cpu: *const u8) {
    pm_metal_boot_banner(version, cpu);
}

#[inline]
pub unsafe fn emit(line: *const u8) {
    pm_metal_boot_emit(line);
}

#[inline]
pub fn tree_ready_ok() {
    unsafe { pm_metal_boot_tree_ready_ok() }
}

#[inline]
pub fn tree_print() -> i32 {
    unsafe { pm_metal_boot_tree_print() }
}
