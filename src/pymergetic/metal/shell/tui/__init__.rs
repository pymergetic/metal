//! shell.tui — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

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
    fn pm_metal_tui_init() -> i32;
    fn pm_metal_tui_paint_vt(vt_index: i32) -> i32;
    fn pm_metal_tui_render_vt(vt_index: i32) -> i32;
    fn pm_metal_tui_render_draw(ds: *mut pm_metal_draw_surface_t) -> i32;
}

#[inline] pub fn init() -> i32 { unsafe { pm_metal_tui_init() } }
#[inline] pub fn paint_vt(vt_index: i32) -> i32 { unsafe { pm_metal_tui_paint_vt(vt_index) } }
#[inline] pub fn render_vt(vt_index: i32) -> i32 { unsafe { pm_metal_tui_render_vt(vt_index) } }
#[inline] pub unsafe fn render_draw(ds: *mut pm_metal_draw_surface_t) -> i32 { pm_metal_tui_render_draw(ds) }
