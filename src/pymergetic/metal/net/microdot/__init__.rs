//! net.microdot — Rust face over the C into-Py bridge (`bridge.c`).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_microdot_resolve(attr: *const c_char) -> u32;
    fn pm_metal_net_microdot_new() -> u32;
    fn pm_metal_net_microdot_close(h: u32);
    fn pm_metal_net_microdot_version(buf: *mut c_char, buf_len: usize) -> i32;
    fn pm_metal_net_microdot_request() -> u32;
    fn pm_metal_net_microdot_response() -> u32;
    fn pm_metal_net_microdot_abort() -> u32;
    fn pm_metal_net_microdot_redirect() -> u32;
    fn pm_metal_net_microdot_send_file() -> u32;
    fn pm_metal_net_microdot_url_pattern() -> u32;
    fn pm_metal_net_microdot_async_bytes_io() -> u32;
    fn pm_metal_net_microdot_iscoroutine() -> u32;
    fn pm_metal_net_microdot_getattr(h: u32, attr: *const c_char) -> u32;
    fn pm_metal_net_microdot_call0(h: u32) -> u32;
    fn pm_metal_net_microdot_call_method0(h: u32, method: *const c_char) -> u32;
    fn pm_metal_net_microdot_call_method1(
        h: u32,
        method: *const c_char,
        arg: *const c_char,
    ) -> u32;
    fn pm_metal_net_microdot_route(app_h: u32) -> u32;
    fn pm_metal_net_microdot_run(app_h: u32) -> u32;
    fn pm_metal_net_microdot_get(app_h: u32) -> u32;
    fn pm_metal_net_microdot_post(app_h: u32) -> u32;
}

#[inline]
pub unsafe fn resolve(attr: *const c_char) -> u32 {
    pm_metal_net_microdot_resolve(attr)
}
#[inline]
pub fn new() -> u32 {
    unsafe { pm_metal_net_microdot_new() }
}
#[inline]
pub fn close(h: u32) {
    unsafe { pm_metal_net_microdot_close(h) }
}
#[inline]
pub unsafe fn version(buf: *mut c_char, buf_len: usize) -> i32 {
    pm_metal_net_microdot_version(buf, buf_len)
}
#[inline]
pub fn request() -> u32 {
    unsafe { pm_metal_net_microdot_request() }
}
#[inline]
pub fn response() -> u32 {
    unsafe { pm_metal_net_microdot_response() }
}
#[inline]
pub fn abort() -> u32 {
    unsafe { pm_metal_net_microdot_abort() }
}
#[inline]
pub fn redirect() -> u32 {
    unsafe { pm_metal_net_microdot_redirect() }
}
#[inline]
pub fn send_file() -> u32 {
    unsafe { pm_metal_net_microdot_send_file() }
}
#[inline]
pub fn url_pattern() -> u32 {
    unsafe { pm_metal_net_microdot_url_pattern() }
}
#[inline]
pub fn async_bytes_io() -> u32 {
    unsafe { pm_metal_net_microdot_async_bytes_io() }
}
#[inline]
pub fn iscoroutine() -> u32 {
    unsafe { pm_metal_net_microdot_iscoroutine() }
}
#[inline]
pub unsafe fn getattr(h: u32, attr: *const c_char) -> u32 {
    pm_metal_net_microdot_getattr(h, attr)
}
#[inline]
pub fn call0(h: u32) -> u32 {
    unsafe { pm_metal_net_microdot_call0(h) }
}
#[inline]
pub unsafe fn call_method0(h: u32, method: *const c_char) -> u32 {
    pm_metal_net_microdot_call_method0(h, method)
}
#[inline]
pub unsafe fn call_method1(h: u32, method: *const c_char, arg: *const c_char) -> u32 {
    pm_metal_net_microdot_call_method1(h, method, arg)
}
#[inline]
pub fn route(app_h: u32) -> u32 {
    unsafe { pm_metal_net_microdot_route(app_h) }
}
#[inline]
pub fn run(app_h: u32) -> u32 {
    unsafe { pm_metal_net_microdot_run(app_h) }
}
#[inline]
pub fn get(app_h: u32) -> u32 {
    unsafe { pm_metal_net_microdot_get(app_h) }
}
#[inline]
pub fn post(app_h: u32) -> u32 {
    unsafe { pm_metal_net_microdot_post(app_h) }
}
