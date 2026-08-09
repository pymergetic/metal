//! boot.tree — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_boot_set_print(fn_: unsafe extern "C" fn(line: *const c_char, user: *mut c_void), user: *mut c_void);
    fn pm_metal_boot_emit(line: *const c_char);
    fn pm_metal_boot_banner(version: *const c_char, cpu: *const c_char);
    fn pm_metal_boot_tree_reset();
    fn pm_metal_boot_tree_enter(name: *const c_char);
    fn pm_metal_boot_tree_enter_ex(name: *const c_char, st: i32, detail: *const c_char);
    fn pm_metal_boot_tree_item(name: *const c_char, st: i32, detail: *const c_char);
    fn pm_metal_boot_tree_leave();
    fn pm_metal_boot_tree_ready_ok();
    fn pm_metal_boot_rainbow_metalpython(version: *const c_char, cpu: *const c_char);
    fn pm_metal_boot_tree_print() -> i32;
}

#[inline] pub unsafe fn set_print(fn_: unsafe extern "C" fn(line: *const c_char, user: *mut c_void), user: *mut c_void) { pm_metal_boot_set_print(fn_, user) }
#[inline] pub unsafe fn emit(line: *const c_char) { pm_metal_boot_emit(line) }
#[inline] pub unsafe fn banner(version: *const c_char, cpu: *const c_char) { pm_metal_boot_banner(version, cpu) }
#[inline] pub fn tree_reset() { unsafe { pm_metal_boot_tree_reset() } }
#[inline] pub unsafe fn tree_enter(name: *const c_char) { pm_metal_boot_tree_enter(name) }
#[inline] pub unsafe fn tree_enter_ex(name: *const c_char, st: i32, detail: *const c_char) { pm_metal_boot_tree_enter_ex(name, st, detail) }
#[inline] pub unsafe fn tree_item(name: *const c_char, st: i32, detail: *const c_char) { pm_metal_boot_tree_item(name, st, detail) }
#[inline] pub fn tree_leave() { unsafe { pm_metal_boot_tree_leave() } }
#[inline] pub fn tree_ready_ok() { unsafe { pm_metal_boot_tree_ready_ok() } }
#[inline] pub unsafe fn rainbow_metalpython(version: *const c_char, cpu: *const c_char) { pm_metal_boot_rainbow_metalpython(version, cpu) }
#[inline] pub fn tree_print() -> i32 { unsafe { pm_metal_boot_tree_print() } }
