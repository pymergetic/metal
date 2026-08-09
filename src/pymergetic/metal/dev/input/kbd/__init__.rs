//! dev.input.kbd — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_kbd_init() -> i32;
    fn pm_metal_kbd_ready() -> i32;
    fn pm_metal_kbd_set_fn_callback(cb: unsafe extern "C" fn(fn_key: i32, user: *mut c_void), user: *mut c_void);
    fn pm_metal_kbd_feed_scancode(scancode: u8);
    fn pm_metal_kbd_poll();
}

#[inline] pub fn init() -> i32 { unsafe { pm_metal_kbd_init() } }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_kbd_ready() } }
#[inline] pub unsafe fn set_fn_callback(cb: unsafe extern "C" fn(fn_key: i32, user: *mut c_void), user: *mut c_void) { pm_metal_kbd_set_fn_callback(cb, user) }
#[inline] pub fn feed_scancode(scancode: u8) { unsafe { pm_metal_kbd_feed_scancode(scancode) } }
#[inline] pub fn poll() { unsafe { pm_metal_kbd_poll() } }
