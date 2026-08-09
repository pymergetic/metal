//! dev.gfx.scanout — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

use pymergetic_metal_rt as _;

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

#[repr(C)]
pub struct pm_metal_scanout_ops_t {
    _opaque: [u8; 0],
}

extern "C" {
    fn pm_metal_scanout_bind(b: *const pm_metal_scanout_bind_t) -> i32;
    fn pm_metal_scanout_ops() -> *const pm_metal_scanout_ops_t;
    fn pm_metal_scanout_name() -> *const c_char;
    fn pm_metal_scanout_caps() -> u32;
    fn pm_metal_scanout_fini();
    fn pm_metal_scanout_bind_info() -> *const pm_metal_scanout_bind_t;
    fn pm_metal_scanout_bind_set_shadow(pixels: *mut u32, pitch: u32);
    fn pm_metal_scanout_copy_rect(dst: *mut u32, dst_pitch: u32, x: i32, y: i32, w: i32, h: i32, b: *const pm_metal_scanout_bind_t);
}

#[inline] pub unsafe fn bind(b: *const pm_metal_scanout_bind_t) -> i32 { pm_metal_scanout_bind(b) }
#[inline] pub unsafe fn ops() -> *const pm_metal_scanout_ops_t { pm_metal_scanout_ops() }
#[inline] pub unsafe fn name() -> *const c_char { pm_metal_scanout_name() }
#[inline] pub fn caps() -> u32 { unsafe { pm_metal_scanout_caps() } }
#[inline] pub fn fini() { unsafe { pm_metal_scanout_fini() } }
#[inline] pub unsafe fn bind_info() -> *const pm_metal_scanout_bind_t { pm_metal_scanout_bind_info() }
#[inline] pub unsafe fn bind_set_shadow(pixels: *mut u32, pitch: u32) { pm_metal_scanout_bind_set_shadow(pixels, pitch) }
#[inline] pub unsafe fn copy_rect(dst: *mut u32, dst_pitch: u32, x: i32, y: i32, w: i32, h: i32, b: *const pm_metal_scanout_bind_t) { pm_metal_scanout_copy_rect(dst, dst_pitch, x, y, w, h, b) }
