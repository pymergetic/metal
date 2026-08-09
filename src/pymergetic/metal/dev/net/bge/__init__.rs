//! dev.net.bge — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_bge_netif_detect() -> i32;
    fn pm_metal_bge_netif_open(mac_out: *mut u8) -> i32;
    fn pm_metal_bge_netif_ready() -> i32;
    fn pm_metal_bge_netif_mac() -> *const u8;
    fn pm_metal_bge_netif_tx(frame: *const c_void, len: u32) -> i32;
    fn pm_metal_bge_netif_poll(on_frame: unsafe extern "C" fn(ctx: *mut c_void, frame: *const u8, len: u32), ctx: *mut c_void);
}

#[inline] pub fn detect() -> i32 { unsafe { pm_metal_bge_netif_detect() } }
#[inline] pub unsafe fn open(mac_out: *mut u8) -> i32 { pm_metal_bge_netif_open(mac_out) }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_bge_netif_ready() } }
#[inline] pub unsafe fn mac() -> *const u8 { pm_metal_bge_netif_mac() }
#[inline] pub unsafe fn tx(frame: *const c_void, len: u32) -> i32 { pm_metal_bge_netif_tx(frame, len) }
#[inline] pub unsafe fn poll(on_frame: unsafe extern "C" fn(ctx: *mut c_void, frame: *const u8, len: u32), ctx: *mut c_void) { pm_metal_bge_netif_poll(on_frame, ctx) }
