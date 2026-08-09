//! console — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_console_init(buf: *mut u8, cap: usize) -> i32;
    fn pm_metal_console_create(id: i32, buf: *mut u8, cap: usize) -> i32;
    fn pm_metal_console_ready() -> i32;
    fn pm_metal_console_ready_id(id: i32) -> i32;
    fn pm_metal_console_write(data: *const u8, n: usize) -> usize;
    fn pm_metal_console_write_id(id: i32, data: *const u8, n: usize) -> usize;
    fn pm_metal_console_viewport_attach(console_id: i32, sink: unsafe extern "C" fn(data: *const u8, n: usize, user: *mut c_void), user: *mut c_void) -> i32;
    fn pm_metal_console_attach(sink: unsafe extern "C" fn(data: *const u8, n: usize, user: *mut c_void), user: *mut c_void) -> i32;
    fn pm_metal_console_viewport_rebind(vp: i32, console_id: i32) -> i32;
    fn pm_metal_console_viewport_detach(vp: i32);
    fn pm_metal_console_detach();
    fn pm_metal_console_set_sink(sink: unsafe extern "C" fn(data: *const u8, n: usize, user: *mut c_void), user: *mut c_void) -> i32;
    fn pm_metal_console_seq() -> u64;
    fn pm_metal_console_len() -> usize;
    fn pm_metal_console_copy_tail(out: *mut u8, cap: usize) -> usize;
}

#[inline] pub unsafe fn init(buf: *mut u8, cap: usize) -> i32 { pm_metal_console_init(buf, cap) }
#[inline] pub unsafe fn create(id: i32, buf: *mut u8, cap: usize) -> i32 { pm_metal_console_create(id, buf, cap) }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_console_ready() } }
#[inline] pub fn ready_id(id: i32) -> i32 { unsafe { pm_metal_console_ready_id(id) } }
#[inline] pub unsafe fn write(data: *const u8, n: usize) -> usize { pm_metal_console_write(data, n) }
#[inline] pub unsafe fn write_id(id: i32, data: *const u8, n: usize) -> usize { pm_metal_console_write_id(id, data, n) }
#[inline] pub unsafe fn viewport_attach(console_id: i32, sink: unsafe extern "C" fn(data: *const u8, n: usize, user: *mut c_void), user: *mut c_void) -> i32 { pm_metal_console_viewport_attach(console_id, sink, user) }
#[inline] pub unsafe fn attach(sink: unsafe extern "C" fn(data: *const u8, n: usize, user: *mut c_void), user: *mut c_void) -> i32 { pm_metal_console_attach(sink, user) }
#[inline] pub fn viewport_rebind(vp: i32, console_id: i32) -> i32 { unsafe { pm_metal_console_viewport_rebind(vp, console_id) } }
#[inline] pub fn viewport_detach(vp: i32) { unsafe { pm_metal_console_viewport_detach(vp) } }
#[inline] pub fn detach() { unsafe { pm_metal_console_detach() } }
#[inline] pub unsafe fn set_sink(sink: unsafe extern "C" fn(data: *const u8, n: usize, user: *mut c_void), user: *mut c_void) -> i32 { pm_metal_console_set_sink(sink, user) }
#[inline] pub fn seq() -> u64 { unsafe { pm_metal_console_seq() } }
#[inline] pub fn len() -> usize { unsafe { pm_metal_console_len() } }
#[inline] pub unsafe fn copy_tail(out: *mut u8, cap: usize) -> usize { pm_metal_console_copy_tail(out, cap) }
