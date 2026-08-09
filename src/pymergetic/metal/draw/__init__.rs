//! draw — Rust face over the C impl.
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

extern "C" {
    fn pm_metal_draw_soft_init(s: *mut pm_metal_draw_surface_t, buf: *mut u8, width: u32, height: u32, bpp: u8) -> i32;
    fn pm_metal_draw_fill(s: *mut pm_metal_draw_surface_t, argb: u32);
    fn pm_metal_draw_pixel(s: *mut pm_metal_draw_surface_t, x: i32, y: i32, argb: u32);
    fn pm_metal_draw_glyph8(s: *mut pm_metal_draw_surface_t, x: i32, y: i32, ch: char, fg: u32, bg: u32);
    fn pm_metal_draw_text8(s: *mut pm_metal_draw_surface_t, x: i32, y: i32, text: *const c_char, fg: u32, bg: u32);
    fn pm_metal_draw_checksum(s: *const pm_metal_draw_surface_t) -> u32;
}

#[inline] pub unsafe fn soft_init(s: *mut pm_metal_draw_surface_t, buf: *mut u8, width: u32, height: u32, bpp: u8) -> i32 { pm_metal_draw_soft_init(s, buf, width, height, bpp) }
#[inline] pub unsafe fn fill(s: *mut pm_metal_draw_surface_t, argb: u32) { pm_metal_draw_fill(s, argb) }
#[inline] pub unsafe fn pixel(s: *mut pm_metal_draw_surface_t, x: i32, y: i32, argb: u32) { pm_metal_draw_pixel(s, x, y, argb) }
#[inline] pub unsafe fn glyph8(s: *mut pm_metal_draw_surface_t, x: i32, y: i32, ch: char, fg: u32, bg: u32) { pm_metal_draw_glyph8(s, x, y, ch, fg, bg) }
#[inline] pub unsafe fn text8(s: *mut pm_metal_draw_surface_t, x: i32, y: i32, text: *const c_char, fg: u32, bg: u32) { pm_metal_draw_text8(s, x, y, text, fg, bg) }
#[inline] pub unsafe fn checksum(s: *const pm_metal_draw_surface_t) -> u32 { pm_metal_draw_checksum(s) }
