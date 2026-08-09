//! net.asgi — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_asgi_init(port: u16) -> i32;
    fn pm_metal_asgi_init_tls(port: u16) -> i32;
    fn pm_metal_asgi_poll() -> i32;
    fn pm_metal_asgi_ready() -> i32;
}

#[inline] pub fn init(port: u16) -> i32 { unsafe { pm_metal_asgi_init(port) } }
#[inline] pub fn init_tls(port: u16) -> i32 { unsafe { pm_metal_asgi_init_tls(port) } }
#[inline] pub fn poll() -> i32 { unsafe { pm_metal_asgi_poll() } }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_asgi_ready() } }
