//! Gfx — shadow compositor + scanout probe
//! (virtio_gpu / bochs_flip / radeon_rv370 / i915_855gm / gop_blt / lfb_copy).
//!
//! PCI display class still lands via `bus/pci`; this module harvests Bochs
//! DISPI or EFI GOP LFB, binds a scanout backend, and presents.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code)]
#![allow(unexpected_cfgs)]

#[path = "_bochs.rs"]
mod bochs;
#[path = "_compositor.rs"]
mod compositor;
#[path = "_font.rs"]
mod font;
#[path = "_gop.rs"]
mod gop;
#[path = "_harvest.rs"]
mod harvest;
#[path = "_i915.rs"]
mod i915;
#[path = "_lfb.rs"]
mod lfb;
#[path = "_radeon.rs"]
mod radeon;
#[path = "_scanout.rs"]
mod scanout;
#[path = "_ui.rs"]
mod ui;
#[path = "_virtio_gpu.rs"]
mod virtio_gpu;

use core::cell::Cell;
use core::ffi::c_void;

use pymergetic_metal_async as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_rt as _;
use pymergetic_metal_dt as _;
use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};

/// Probe framebuffer, init compositor, present proof stripe.
/// Returns 0 if no FB (serial-only) or present ok; -1 on init/present fail.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_detect() -> i32 {
    compositor::detect_and_present()
}

/// Pre-ExitBootServices GOP Blt present when EFI stashed a live protocol.
/// No-op (returns 0) on BIOS / no GOP. Call before `leave_firmware`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_efi_pre_ebs() -> i32 {
    compositor::efi_pre_ebs()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_init() -> i32 {
    compositor::init()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_ready() -> i32 {
    compositor::ready()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_width() -> i32 {
    compositor::width()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_height() -> i32 {
    compositor::height()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_clear(color: u32) {
    compositor::clear(color)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_fill_rect(
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    color: u32,
) {
    compositor::fill_rect(x, y, w, h, color)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_draw_text(
    x: i32,
    y: i32,
    text: *const u8,
    fg: u32,
    bg: u32,
    transparent_bg: i32,
) {
    compositor::draw_text(x, y, text, fg, bg, transparent_bg)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_present() -> i32 {
    compositor::present()
}

/// Blit guest/host BGRA into the shadow FB (no present).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_blit_bgra(
    dx: i32,
    dy: i32,
    dw: i32,
    dh: i32,
    pixels: *const u8,
    src_w: i32,
    src_h: i32,
    src_pitch: i32,
) -> i32 {
    compositor::blit_bgra(dx, dy, dw, dh, pixels, src_w, src_h, src_pitch)
}

/// Present then return an awaitable yield handle (surface id reserved for multi-surf).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_present_async(surface: u32) -> u32 {
    let _ = surface;
    extern "C" {
        fn pm_metal_async_yield() -> u32;
    }
    let _ = compositor::present();
    pm_metal_async_yield()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_scanout_name() -> *const u8 {
    match scanout::name() {
        "virtio_gpu" => b"virtio_gpu\0".as_ptr(),
        "bochs_flip" => b"bochs_flip\0".as_ptr(),
        "radeon_rv370" => b"radeon_rv370\0".as_ptr(),
        "i915_855gm" => b"i915_855gm\0".as_ptr(),
        "gop_blt" => b"gop_blt\0".as_ptr(),
        "lfb_copy" => b"lfb_copy\0".as_ptr(),
        _ => b"none\0".as_ptr(),
    }
}

static FLOOR_ENTRIES: RegModStatic<13, 0> = RegModStatic::new(
    [
        RegEntry::new("pm_metal_dev_gfx_detect"),
        RegEntry::new("pm_metal_dev_gfx_efi_pre_ebs"),
        RegEntry::new("pm_metal_dev_gfx_init"),
        RegEntry::new("pm_metal_dev_gfx_ready"),
        RegEntry::new("pm_metal_dev_gfx_width"),
        RegEntry::new("pm_metal_dev_gfx_height"),
        RegEntry::new("pm_metal_dev_gfx_clear"),
        RegEntry::new("pm_metal_dev_gfx_fill_rect"),
        RegEntry::new("pm_metal_dev_gfx_draw_text"),
        RegEntry::new("pm_metal_dev_gfx_present"),
        RegEntry::new("pm_metal_dev_gfx_blit_bgra"),
        RegEntry::new("pm_metal_dev_gfx_present_async"),
        RegEntry::new("pm_metal_dev_gfx_scanout_name"),
    ],
    [],
);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[
            pm_metal_dev_gfx_detect as *const c_void,
            pm_metal_dev_gfx_efi_pre_ebs as *const c_void,
            pm_metal_dev_gfx_init as *const c_void,
            pm_metal_dev_gfx_ready as *const c_void,
            pm_metal_dev_gfx_width as *const c_void,
            pm_metal_dev_gfx_height as *const c_void,
            pm_metal_dev_gfx_clear as *const c_void,
            pm_metal_dev_gfx_fill_rect as *const c_void,
            pm_metal_dev_gfx_draw_text as *const c_void,
            pm_metal_dev_gfx_present as *const c_void,
            pm_metal_dev_gfx_blit_bgra as *const c_void,
            pm_metal_dev_gfx_present_async as *const c_void,
            pm_metal_dev_gfx_scanout_name as *const c_void,
        ],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.dev.gfx",
    unloadable: false,
    parent: None,
    ctx: core::ptr::null_mut(),
    on_load: None,
    register_symbols: Some(floor_register_symbols),
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: None,
    on_unload: None,
    entries: &FLOOR_ENTRIES.entries,
    imports: &[],
    raw_next: Cell::new(core::ptr::null()),
    raw_prev: Cell::new(core::ptr::null()),
};

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_mod_load() -> i32 {
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
