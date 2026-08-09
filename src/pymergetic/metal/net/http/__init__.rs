//! net.http — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_http_init() -> i32;
    fn pm_metal_net_http_init_tls() -> i32;
    fn pm_metal_net_http_shutdown();
    fn pm_metal_net_http_poll() -> i32;
    fn pm_metal_net_http_served() -> i32;
    fn pm_metal_net_http_client_get(host: *const c_char, port: u16, path: *const c_char, buf: *mut u8, cap: u32, len_out: *mut u32) -> i32;
    fn pm_metal_net_http_get(url: *const c_char) -> u32;
    fn pm_metal_net_http_status() -> u32;
    fn pm_metal_net_http_body_len() -> u32;
    fn pm_metal_net_http_body() -> *const u8;
    fn pm_metal_net_http_set_tls_verify_none(on: i32);
    fn pm_metal_net_http_client_poll();
}

#[inline] pub fn init() -> i32 { unsafe { pm_metal_net_http_init() } }
#[inline] pub fn init_tls() -> i32 { unsafe { pm_metal_net_http_init_tls() } }
#[inline] pub fn shutdown() { unsafe { pm_metal_net_http_shutdown() } }
#[inline] pub fn poll() -> i32 { unsafe { pm_metal_net_http_poll() } }
#[inline] pub fn served() -> i32 { unsafe { pm_metal_net_http_served() } }
#[inline] pub unsafe fn client_get(host: *const c_char, port: u16, path: *const c_char, buf: *mut u8, cap: u32, len_out: *mut u32) -> i32 { pm_metal_net_http_client_get(host, port, path, buf, cap, len_out) }
#[inline] pub unsafe fn get(url: *const c_char) -> u32 { pm_metal_net_http_get(url) }
#[inline] pub fn status() -> u32 { unsafe { pm_metal_net_http_status() } }
#[inline] pub fn body_len() -> u32 { unsafe { pm_metal_net_http_body_len() } }
#[inline] pub unsafe fn body() -> *const u8 { pm_metal_net_http_body() }
#[inline] pub fn set_tls_verify_none(on: i32) { unsafe { pm_metal_net_http_set_tls_verify_none(on) } }
#[inline] pub fn client_poll() { unsafe { pm_metal_net_http_client_poll() } }
