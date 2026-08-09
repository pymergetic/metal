//! dev.gfx.compositor — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_gfx_surface_t {
    pub pixels: *mut u32,
    pub width: u32,
    pub height: u32,
    pub pitch: u32,
}

#[repr(C)]
pub struct pm_metal_scanout_bind_t {
    pub shadow: *mut u32,
    pub shadow_w: u32,
    pub shadow_h: u32,
    pub shadow_pitch: u32,
    pub fb: *mut u32,
    pub fb_ppsl: u32,
    pub mode_w: u32,
    pub mode_h: u32,
    pub gop: *mut c_void,
    pub owned: i32,
}

extern "C" {
    fn pm_metal_gfx_init_from_bind(harvest: *const pm_metal_scanout_bind_t) -> i32;
    fn pm_metal_gfx_init() -> i32;
    fn pm_metal_gfx_fini();
    fn pm_metal_gfx_ready() -> i32;
    fn pm_metal_gfx_surface() -> *mut pm_metal_gfx_surface_t;
    fn pm_metal_gfx_scanout_name() -> *const c_char;
    fn pm_metal_gfx_clear(color: u32);
    fn pm_metal_gfx_fill_rect(x: i32, y: i32, w: i32, h: i32, color: u32);
    fn pm_metal_gfx_present_rect(x: i32, y: i32, w: i32, h: i32) -> i32;
    fn pm_metal_gfx_present() -> i32;
}

#[inline] pub unsafe fn init_from_bind(harvest: *const pm_metal_scanout_bind_t) -> i32 { pm_metal_gfx_init_from_bind(harvest) }
#[inline] pub fn init() -> i32 { unsafe { pm_metal_gfx_init() } }
#[inline] pub fn fini() { unsafe { pm_metal_gfx_fini() } }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_gfx_ready() } }
#[inline] pub unsafe fn surface() -> *mut pm_metal_gfx_surface_t { pm_metal_gfx_surface() }
#[inline] pub unsafe fn scanout_name() -> *const c_char { pm_metal_gfx_scanout_name() }
#[inline] pub fn clear(color: u32) { unsafe { pm_metal_gfx_clear(color) } }
#[inline] pub fn fill_rect(x: i32, y: i32, w: i32, h: i32, color: u32) { unsafe { pm_metal_gfx_fill_rect(x, y, w, h, color) } }
#[inline] pub fn present_rect(x: i32, y: i32, w: i32, h: i32) -> i32 { unsafe { pm_metal_gfx_present_rect(x, y, w, h) } }
#[inline] pub fn present() -> i32 { unsafe { pm_metal_gfx_present() } }
