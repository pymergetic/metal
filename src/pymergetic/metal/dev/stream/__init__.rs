//! dev.stream — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_stream_termios_t {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct pm_metal_stream_winsize_t {
    _opaque: [u8; 0],
}

extern "C" {
    fn pm_metal_stream_pipe(read_end: *mut pm_metal_stream_h, write_end: *mut pm_metal_stream_h) -> i32;
    fn pm_metal_stream_pty(master: *mut pm_metal_stream_h, slave: *mut pm_metal_stream_h) -> i32;
    fn pm_metal_stream_write(h: u32, ptr: *const c_void, len: u32) -> u32;
    fn pm_metal_stream_try_read(h: u32, ptr: *mut c_void, len: u32) -> u32;
    fn pm_metal_stream_read(h: u32, ptr: *mut c_void, len: u32) -> u32;
    fn pm_metal_stream_drain(h: u32) -> u32;
    fn pm_metal_stream_close(h: u32);
    fn pm_metal_stream_termios_get(h: u32, out: *mut pm_metal_stream_termios_t) -> i32;
    fn pm_metal_stream_termios_set(h: u32, in: *const pm_metal_stream_termios_t) -> i32;
    fn pm_metal_stream_winsize_get(h: u32, out: *mut pm_metal_stream_winsize_t) -> i32;
    fn pm_metal_stream_winsize_set(h: u32, in: *const pm_metal_stream_winsize_t) -> i32;
    fn pm_metal_stream_pending(h: u32) -> u32;
    fn pm_metal_stdio_attach(in: u32, out: u32, err: u32) -> i32;
    fn pm_metal_stdio_in() -> u32;
    fn pm_metal_stdio_out() -> u32;
    fn pm_metal_stdio_err() -> u32;
    fn pm_metal_stream_feed_stdin(ptr: *const c_void, len: u32) -> u32;
    fn pm_metal_stream_write_line(h: u32, line: *const c_char) -> u32;
}

#[inline] pub unsafe fn pipe(read_end: *mut pm_metal_stream_h, write_end: *mut pm_metal_stream_h) -> i32 { pm_metal_stream_pipe(read_end, write_end) }
#[inline] pub unsafe fn pty(master: *mut pm_metal_stream_h, slave: *mut pm_metal_stream_h) -> i32 { pm_metal_stream_pty(master, slave) }
#[inline] pub unsafe fn write(h: u32, ptr: *const c_void, len: u32) -> u32 { pm_metal_stream_write(h, ptr, len) }
#[inline] pub unsafe fn try_read(h: u32, ptr: *mut c_void, len: u32) -> u32 { pm_metal_stream_try_read(h, ptr, len) }
#[inline] pub unsafe fn read(h: u32, ptr: *mut c_void, len: u32) -> u32 { pm_metal_stream_read(h, ptr, len) }
#[inline] pub fn drain(h: u32) -> u32 { unsafe { pm_metal_stream_drain(h) } }
#[inline] pub fn close(h: u32) { unsafe { pm_metal_stream_close(h) } }
#[inline] pub unsafe fn termios_get(h: u32, out: *mut pm_metal_stream_termios_t) -> i32 { pm_metal_stream_termios_get(h, out) }
#[inline] pub unsafe fn termios_set(h: u32, in: *const pm_metal_stream_termios_t) -> i32 { pm_metal_stream_termios_set(h, in) }
#[inline] pub unsafe fn winsize_get(h: u32, out: *mut pm_metal_stream_winsize_t) -> i32 { pm_metal_stream_winsize_get(h, out) }
#[inline] pub unsafe fn winsize_set(h: u32, in: *const pm_metal_stream_winsize_t) -> i32 { pm_metal_stream_winsize_set(h, in) }
#[inline] pub fn pending(h: u32) -> u32 { unsafe { pm_metal_stream_pending(h) } }
#[inline] pub fn attach(in: u32, out: u32, err: u32) -> i32 { unsafe { pm_metal_stdio_attach(in, out, err) } }
#[inline] pub fn in() -> u32 { unsafe { pm_metal_stdio_in() } }
#[inline] pub fn out() -> u32 { unsafe { pm_metal_stdio_out() } }
#[inline] pub fn err() -> u32 { unsafe { pm_metal_stdio_err() } }
#[inline] pub unsafe fn feed_stdin(ptr: *const c_void, len: u32) -> u32 { pm_metal_stream_feed_stdin(ptr, len) }
#[inline] pub unsafe fn write_line(h: u32, line: *const c_char) -> u32 { pm_metal_stream_write_line(h, line) }
