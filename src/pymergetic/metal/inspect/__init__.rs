//! inspect — Rust face over the C ABI (`inspect/__init__.h` + `py_call.h`).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_inspect_init() -> i32;
    fn pm_metal_inspect_capabilities_json(buf: *mut c_char, buf_len: usize) -> i32;
    fn pm_metal_inspect_handle(
        method: *const c_char,
        path: *const c_char,
        status: *mut i32,
        body: *mut c_char,
        body_len: usize,
    ) -> i32;
    fn pm_metal_inspect_py_handle(
        method: *const c_char,
        path: *const c_char,
        status: *mut i32,
        body: *mut c_char,
        body_len: usize,
    ) -> i32;
}

#[inline]
pub fn init() -> i32 {
    unsafe { pm_metal_inspect_init() }
}
#[inline]
pub unsafe fn capabilities_json(buf: *mut c_char, buf_len: usize) -> i32 {
    pm_metal_inspect_capabilities_json(buf, buf_len)
}
#[inline]
pub unsafe fn handle(
    method: *const c_char,
    path: *const c_char,
    status: *mut i32,
    body: *mut c_char,
    body_len: usize,
) -> i32 {
    pm_metal_inspect_handle(method, path, status, body, body_len)
}
#[inline]
pub unsafe fn py_handle(
    method: *const c_char,
    path: *const c_char,
    status: *mut i32,
    body: *mut c_char,
    body_len: usize,
) -> i32 {
    pm_metal_inspect_py_handle(method, path, status, body, body_len)
}
