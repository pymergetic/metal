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


use core::ffi::c_void;

use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

pymergetic_metal_reg::reg_mod! {
    mod async_mod = "pymergetic.metal.async";
    exports: [
        yield_ = "yield",
        start,
        ready,
        run_poll,
        run_poll_all,
        sleep_us
    ];
}

extern "C" fn async_register_symbols(_ctx: *mut c_void) -> i32 {
    async_mod::yield_.publish(pm_metal_async_yield as *const c_void);
    async_mod::start.publish(pm_metal_async_start as *const c_void);
    async_mod::ready.publish(pm_metal_async_ready as *const c_void);
    async_mod::run_poll.publish(pm_metal_async_run_poll as *const c_void);
    async_mod::run_poll_all.publish(pm_metal_async_run_poll_all as *const c_void);
    async_mod::sleep_us.publish(pm_metal_async_sleep_us as *const c_void);
    0
}

static ASYNC_MOD: RegMod = RegMod::from_static(
    async_mod::NAME,
    &async_mod::STORAGE.exports,
    &async_mod::STORAGE.imports,
    Some(async_register_symbols),
);

#[no_mangle]
pub extern "C" fn pm_metal_async_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod(async_mod::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&ASYNC_MOD) }
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_async_reg_load()
}
