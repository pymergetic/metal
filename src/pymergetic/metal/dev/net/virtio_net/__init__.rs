//! dev.net.virtio_net — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_dev_net_virtio_probe(mac_out: *mut u8) -> i32;
    fn pm_metal_dev_net_virtio_open(mac_out: *mut u8) -> i32;
    fn pm_metal_dev_net_virtio_ready() -> i32;
    fn pm_metal_dev_net_virtio_mac() -> *const u8;
    fn pm_metal_dev_net_virtio_tx(frame: *const c_void, len: u32) -> i32;
    fn pm_metal_dev_net_virtio_poll(on_frame: unsafe extern "C" fn(ctx: *mut c_void, frame: *const u8, len: u32), ctx: *mut c_void);
    fn pm_metal_dev_net_virtio_reap_tx() -> i32;
}

#[inline] pub unsafe fn probe(mac_out: *mut u8) -> i32 { pm_metal_dev_net_virtio_probe(mac_out) }
#[inline] pub unsafe fn open(mac_out: *mut u8) -> i32 { pm_metal_dev_net_virtio_open(mac_out) }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_dev_net_virtio_ready() } }
#[inline] pub unsafe fn mac() -> *const u8 { pm_metal_dev_net_virtio_mac() }
#[inline] pub unsafe fn tx(frame: *const c_void, len: u32) -> i32 { pm_metal_dev_net_virtio_tx(frame, len) }
#[inline] pub unsafe fn poll(on_frame: unsafe extern "C" fn(ctx: *mut c_void, frame: *const u8, len: u32), ctx: *mut c_void) { pm_metal_dev_net_virtio_poll(on_frame, ctx) }
#[inline] pub fn reap_tx() -> i32 { unsafe { pm_metal_dev_net_virtio_reap_tx() } }
