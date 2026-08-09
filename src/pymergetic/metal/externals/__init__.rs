//! externals — Rust face over the C impl (`boot/externals.c`).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_external_t {
    pub id: *const c_char,
    pub version: *const c_char,
    pub url: *const c_char,
    pub note: *const c_char,
}

extern "C" {
    fn pm_metal_externals_init();
    fn pm_metal_externals_seed_fallback();
    fn pm_metal_external_count() -> u32;
    fn pm_metal_external_get(idx: u32, out: *mut pm_metal_external_t) -> i32;
    fn pm_metal_external_find(id: *const c_char, out: *mut pm_metal_external_t) -> i32;
    fn pm_metal_external_register(
        id: *const c_char,
        version: *const c_char,
        url: *const c_char,
        note: *const c_char,
    ) -> i32;
}

#[inline]
pub fn init() {
    unsafe { pm_metal_externals_init() }
}
#[inline]
pub fn seed_fallback() {
    unsafe { pm_metal_externals_seed_fallback() }
}
#[inline]
pub fn count() -> u32 {
    unsafe { pm_metal_external_count() }
}
#[inline]
pub unsafe fn get(idx: u32, out: *mut pm_metal_external_t) -> i32 {
    pm_metal_external_get(idx, out)
}
#[inline]
pub unsafe fn find(id: *const c_char, out: *mut pm_metal_external_t) -> i32 {
    pm_metal_external_find(id, out)
}
#[inline]
pub unsafe fn register(
    id: *const c_char,
    version: *const c_char,
    url: *const c_char,
    note: *const c_char,
) -> i32 {
    pm_metal_external_register(id, version, url, note)
}
