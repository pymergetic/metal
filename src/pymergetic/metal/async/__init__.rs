//! pymergetic.metal.async — Rust export face over the C N-runner impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_reg as _;
use pymergetic_metal_reg::register_rows_bytes;
use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_async_start(n_cpus: u32) -> i32;
    fn pm_metal_async_ready() -> i32;
    fn pm_metal_async_n_runners() -> u32;
    fn pm_metal_async_create_task(h: u32) -> u32;
    fn pm_metal_async_run_poll() -> i32;
    fn pm_metal_async_run_poll_all() -> i32;
    fn pm_metal_async_run_loop() -> i32;
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_async_bind_reg() -> i32 {
    register_rows_bytes(
        b"pymergetic.metal.async\0",
        &[
            (b"start\0", pm_metal_async_start as *const c_void),
            (b"ready\0", pm_metal_async_ready as *const c_void),
            (b"n_runners\0", pm_metal_async_n_runners as *const c_void),
            (b"create_task\0", pm_metal_async_create_task as *const c_void),
            (b"run_poll\0", pm_metal_async_run_poll as *const c_void),
            (b"run_poll_all\0", pm_metal_async_run_poll_all as *const c_void),
            (b"run_loop\0", pm_metal_async_run_loop as *const c_void),
        ],
    )
}
