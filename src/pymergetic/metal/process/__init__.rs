//! process — Rust face over the C process table.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

extern "C" {
    fn pm_metal_process_crown(
        async_handle: u32,
        mode: u32,
        tag: *const u8,
        teardown: Option<unsafe extern "C" fn(u32, *mut c_void)>,
        teardown_user: *mut c_void,
    ) -> u32;
    fn pm_metal_process_quit(pid: u32, code: i32) -> i32;
    fn pm_metal_process_current() -> u32;
    fn pm_metal_process_quit_all(code: i32) -> u32;
    fn pm_metal_process_shutting_down() -> i32;
    fn pm_metal_process_set_shutting_down(on: i32);
}

#[inline]
pub fn crown(
    async_handle: u32,
    mode: u32,
    tag: *const u8,
    teardown: Option<unsafe extern "C" fn(u32, *mut c_void)>,
    teardown_user: *mut c_void,
) -> u32 {
    unsafe { pm_metal_process_crown(async_handle, mode, tag, teardown, teardown_user) }
}

#[inline]
pub fn quit(pid: u32, code: i32) -> i32 {
    unsafe { pm_metal_process_quit(pid, code) }
}

#[inline]
pub fn current() -> u32 {
    unsafe { pm_metal_process_current() }
}

#[inline]
pub fn quit_all(code: i32) -> u32 {
    unsafe { pm_metal_process_quit_all(code) }
}

#[inline]
pub fn shutting_down() -> i32 {
    unsafe { pm_metal_process_shutting_down() }
}

#[inline]
pub fn set_shutting_down(on: i32) {
    unsafe { pm_metal_process_set_shutting_down(on) }
}
