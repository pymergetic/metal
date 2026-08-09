//! dev.gfx.text — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_gfx_draw_text(x: i32, y: i32, text: *const c_char, fg: u32, bg: u32, transparent_bg: i32);
    fn pm_metal_gfx_font_width() -> u32;
    fn pm_metal_gfx_font_height() -> u32;
}

#[inline] pub unsafe fn draw_text(x: i32, y: i32, text: *const c_char, fg: u32, bg: u32, transparent_bg: i32) { pm_metal_gfx_draw_text(x, y, text, fg, bg, transparent_bg) }
#[inline] pub fn font_width() -> u32 { unsafe { pm_metal_gfx_font_width() } }
#[inline] pub fn font_height() -> u32 { unsafe { pm_metal_gfx_font_height() } }
