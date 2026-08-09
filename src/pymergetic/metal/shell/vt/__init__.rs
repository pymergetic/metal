//! shell.vt — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_draw_surface_t {
    pub pixels: *mut u8,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub bpp: u8,
}

#[repr(C)]
pub struct pm_metal_vt_t {
    _opaque: [u8; 0],
}

extern "C" {
    fn pm_metal_vt_init() -> i32;
    fn pm_metal_vt_ready() -> i32;
    fn pm_metal_vt_switch(index: i32) -> i32;
    fn pm_metal_vt_active() -> i32;
    fn pm_metal_vt_get(index: i32) -> *mut pm_metal_vt_t;
    fn pm_metal_vt_bind_surface(index: i32, ds: *mut pm_metal_draw_surface_t);
    fn pm_metal_vt_write(s: *const c_char, n: usize);
    fn pm_metal_vt_puts(s: *const c_char);
    fn pm_metal_vt_render(index: i32) -> i32;
}

#[inline] pub fn init() -> i32 { unsafe { pm_metal_vt_init() } }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_vt_ready() } }
#[inline] pub fn switch(index: i32) -> i32 { unsafe { pm_metal_vt_switch(index) } }
#[inline] pub fn active() -> i32 { unsafe { pm_metal_vt_active() } }
#[inline] pub unsafe fn get(index: i32) -> *mut pm_metal_vt_t { pm_metal_vt_get(index) }
#[inline] pub unsafe fn bind_surface(index: i32, ds: *mut pm_metal_draw_surface_t) { pm_metal_vt_bind_surface(index, ds) }
#[inline] pub unsafe fn write(s: *const c_char, n: usize) { pm_metal_vt_write(s, n) }
#[inline] pub unsafe fn puts(s: *const c_char) { pm_metal_vt_puts(s) }
#[inline] pub fn render(index: i32) -> i32 { unsafe { pm_metal_vt_render(index) } }
