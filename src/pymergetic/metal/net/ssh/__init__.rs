//! net.ssh — Rust export face over the C impl (`__init__.c`).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_reg as _;
use pymergetic_metal_reg::register_rows_bytes;
use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_ssh_listen(port: u32) -> u32;
    fn pm_metal_net_ssh_close(s: u32);
    fn pm_metal_net_ssh_autoload() -> i32;
    fn pm_metal_net_ssh_status(buf: *mut u8, buf_len: u32) -> i32;
    fn pm_metal_net_ssh_listen_port() -> u32;
    fn pm_metal_net_ssh_hostkey_label(buf: *mut u8, buf_len: u32) -> i32;
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ssh_bind_reg() -> i32 {
    register_rows_bytes(
        b"pymergetic.metal.net.ssh\0",
        &[
            (b"listen\0", pm_metal_net_ssh_listen as *const c_void),
            (b"close\0", pm_metal_net_ssh_close as *const c_void),
            (b"autoload\0", pm_metal_net_ssh_autoload as *const c_void),
            (b"status\0", pm_metal_net_ssh_status as *const c_void),
        ],
    )
}
