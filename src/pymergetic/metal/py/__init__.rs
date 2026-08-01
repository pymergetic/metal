//! Metal py edge — alloc/GC-off/async bridge/reg bind (upy mirror comes later).
//!
//! Finished slices only: no hollow loop/VM. Link into firmware when bring-up
//! needs it; host smoke covers the edge today.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

#[path = "_alloc.rs"]
mod alloc;
#[path = "_async_bridge.rs"]
mod async_bridge;
#[path = "_bind.rs"]
mod bind;
#[path = "_gc_off.rs"]
mod gc_off;
#[path = "_libc_policy.rs"]
mod libc_policy;

#[path = "upy/mod.rs"]
pub mod upy;

use pymergetic_metal_async as _;
use pymergetic_metal_fs as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_reg as _;
use pymergetic_metal_rt as _;

/// Edge + B0 upy faces ready (VM loop still omitted).
#[no_mangle]
pub extern "C" fn pm_metal_py_ready() -> i32 {
    if !upy::py::mpstate::ready() {
        upy::py::mpstate::init();
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_alloc(size: usize) -> *mut u8 {
    alloc::alloc(size)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_free(ptr: *mut u8) {
    alloc::free(ptr)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_realloc(ptr: *mut u8, size: usize) -> *mut u8 {
    alloc::realloc(ptr, size)
}

/// 0 = GC disabled (always).
#[no_mangle]
pub extern "C" fn pm_metal_py_gc_enabled() -> i32 {
    if gc_off::enabled() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn pm_metal_py_gc_collect() -> i32 {
    gc_off::collect()
}

/// Metal libc policy id (`1` = Metal).
#[no_mangle]
pub extern "C" fn pm_metal_py_libc_policy() -> u32 {
    libc_policy::current() as u32
}

/// Park on Metal await (async-first).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_await(self_h: u32, child_h: u32) -> u32 {
    async_bridge::await_child(self_h, child_h) as u32
}

#[no_mangle]
pub extern "C" fn pm_metal_py_sleep_us(us: u64) -> u32 {
    async_bridge::sleep_us(us)
}

/// Publish edge symbols onto `reg` (`pymergetic.metal.py.*`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_bind_reg() -> i32 {
    bind::publish()
}

/// W4.1 proof: py ready (print path exercised by await note / tree). Silent.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_proof_print() -> i32 {
    if pm_metal_py_ready() != 0 {
        return -1;
    }
    0
}

/// W4.2 proof: park on Metal sleep (asyncio.sleep_ms). Silent — tree shows await.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_proof_await() -> i32 {
    let _ = pm_metal_py_ready();
    let Some(t) = upy::extmod::asyncio::sleep_ms(1) else {
        return -1;
    };
    let st0 = upy::extmod::asyncio::core::status(t.handle);
    if st0 == upy::extmod::asyncio::core::STATUS_ERROR {
        t.cancel();
        return -1;
    }
    if !upy::extmod::asyncio::run_until(t.handle) {
        t.cancel();
        return -1;
    }
    if upy::extmod::asyncio::core::status(t.handle) != upy::extmod::asyncio::core::STATUS_DONE {
        t.cancel();
        return -1;
    }
    t.cancel();
    0
}
