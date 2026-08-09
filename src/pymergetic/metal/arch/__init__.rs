//! arch — Rust face over the C ABI (`arch.h` / `arch.c` + into-Py `py_call.c`).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum pm_metal_arch_id_t {
    X86 = 0,
    X86_64 = 1,
    Wasm = 2,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum pm_metal_arch_firmware_t {
    None = 0,
    Bios = 1,
    Uefi = 2,
    Browser = 3,
    Unix = 4,
}

extern "C" {
    fn pm_metal_arch_current() -> pm_metal_arch_id_t;
    fn pm_metal_arch_name(id: pm_metal_arch_id_t) -> *const c_char;
    fn pm_metal_arch_firmware() -> pm_metal_arch_firmware_t;
    fn pm_metal_arch_py_name(buf: *mut c_char, buf_len: usize) -> i32;
    fn pm_metal_arch_py_names(buf: *mut c_char, buf_len: usize) -> i32;
}

#[inline]
pub fn current() -> pm_metal_arch_id_t {
    unsafe { pm_metal_arch_current() }
}
#[inline]
pub fn name(id: pm_metal_arch_id_t) -> *const c_char {
    unsafe { pm_metal_arch_name(id) }
}
#[inline]
pub fn firmware() -> pm_metal_arch_firmware_t {
    unsafe { pm_metal_arch_firmware() }
}
#[inline]
pub unsafe fn py_name(buf: *mut c_char, buf_len: usize) -> i32 {
    pm_metal_arch_py_name(buf, buf_len)
}
#[inline]
pub unsafe fn py_names(buf: *mut c_char, buf_len: usize) -> i32 {
    pm_metal_arch_py_names(buf, buf_len)
}
