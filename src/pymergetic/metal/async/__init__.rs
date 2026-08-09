//! async — Rust face over the C N-runner impl + quiesce safepoint muscle.
//!
//! Product LIVE links the C callee for the runner. Quiesce flags live here
//! (`quiesce.rs`) so every seat that links this crate (FW + browser) provides
//! the C ABI symbols `async/__init__.c` checkpoints call.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

#[path = "quiesce.rs"]
pub mod quiesce;

extern "C" {
    fn pm_metal_async_await(self_h: u32, child_h: u32) -> i32;
    fn pm_metal_async_park() -> u32;
    fn pm_metal_async_status(h: u32) -> i32;
    fn pm_metal_async_start(n_cpus: u32) -> i32;
    fn pm_metal_async_ready() -> i32;
    fn pm_metal_async_n_runners() -> u32;
    fn pm_metal_async_create_task(h: u32) -> u32;
    fn pm_metal_async_run_poll() -> i32;
    fn pm_metal_async_run_poll_all() -> i32;
    fn pm_metal_async_run_loop() -> i32;
    fn pm_metal_async_sleep_us(us: u64) -> u32;
    fn pm_metal_async_yield() -> u32;
}

#[inline]
pub fn await_(self_h: u32, child_h: u32) -> i32 {
    unsafe { pm_metal_async_await(self_h, child_h) }
}
#[inline]
pub fn park() -> u32 {
    unsafe { pm_metal_async_park() }
}
#[inline]
pub fn status(h: u32) -> i32 {
    unsafe { pm_metal_async_status(h) }
}
#[inline]
pub fn start(n_cpus: u32) -> i32 {
    unsafe { pm_metal_async_start(n_cpus) }
}
#[inline]
pub fn ready() -> i32 {
    unsafe { pm_metal_async_ready() }
}
#[inline]
pub fn n_runners() -> u32 {
    unsafe { pm_metal_async_n_runners() }
}
#[inline]
pub fn create_task(h: u32) -> u32 {
    unsafe { pm_metal_async_create_task(h) }
}
#[inline]
pub fn run_poll() -> i32 {
    unsafe { pm_metal_async_run_poll() }
}
#[inline]
pub fn run_poll_all() -> i32 {
    unsafe { pm_metal_async_run_poll_all() }
}
#[inline]
pub fn run_loop() -> i32 {
    unsafe { pm_metal_async_run_loop() }
}
#[inline]
pub fn sleep_us(us: u64) -> u32 {
    unsafe { pm_metal_async_sleep_us(us) }
}
#[inline]
pub fn yield_() -> u32 {
    unsafe { pm_metal_async_yield() }
}
