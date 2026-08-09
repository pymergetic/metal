//! boot — Rust face over boot.tree C helpers.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_boot_banner(version: *const u8, cpu: *const u8);
    fn pm_metal_boot_emit(line: *const u8);
    fn pm_metal_boot_tree_ready_ok();
    fn pm_metal_boot_tree_print() -> i32;
    fn pm_metal_boot_unboot() -> i32;
    fn pm_metal_boot_shutting_down() -> i32;
    fn pm_metal_boot_shutdown() -> i32;
    fn pm_metal_boot_reboot() -> i32;
    fn pm_metal_boot_is_dead() -> i32;
    fn pm_metal_boot_clear_dead();
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

#[inline]
pub fn unboot() -> i32 {
    unsafe { pm_metal_boot_unboot() }
}

#[inline]
pub fn shutting_down() -> i32 {
    unsafe { pm_metal_boot_shutting_down() }
}

#[inline]
pub fn shutdown() -> i32 {
    unsafe { pm_metal_boot_shutdown() }
}

#[inline]
pub fn reboot() -> i32 {
    unsafe { pm_metal_boot_reboot() }
}

#[inline]
pub fn is_dead() -> i32 {
    unsafe { pm_metal_boot_is_dead() }
}

#[inline]
pub fn clear_dead() {
    unsafe { pm_metal_boot_clear_dead() }
}
