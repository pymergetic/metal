//! `rt`'s two kinds of cross-module reach, both without a Cargo dependency:
//!
//! - `pm_metal_reg_register` (the registry's own dynamic-table C ABI):
//!   registers `halt`/`panic`/`panic_at` for genuinely late/unloadable
//!   callers (wasm guest code, Python) that cannot Cargo-depend on `rt`.
//! - `console`/`mem`: both depend on `rt`, so a Cargo dependency back would
//!   cycle. Since neither is ever unloaded, a raw `extern "C"` forward
//!   declaration is the correct fast path (resolved at link time, no
//!   runtime lookup) — see `docs/definitions/module.md`. Host builds never
//!   link `console`/`mem` into `rt`'s own test binaries, so these stay
//!   behind the freestanding cfg with a no-op host fallback.

use core::ffi::c_void;

use crate::{pm_metal_rt_halt, pm_metal_rt_panic, pm_metal_rt_panic_at};

const MOD: &[u8] = b"pymergetic.metal.rt\0";

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_reg_register(full_module: *const u8, func: *const u8, ptr: *const c_void) -> i32;
    fn pm_metal_console_ready() -> i32;
    fn pm_metal_console_write(id: u32, s: *const u8, n: usize);
    fn pm_metal_mem_memalign(align: usize, size: usize) -> *mut u8;
    fn pm_metal_mem_free(ptr: *mut u8);
    fn pm_metal_mem_realloc(ptr: *mut u8, size: usize) -> *mut u8;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_reg_register(
    _full_module: *const u8,
    _func: *const u8,
    _ptr: *const c_void,
) -> i32 {
    -1
}

/// Register rt's own dynamically-callable exports onto the registry's
/// dynamic table. Returns 0 or -1 (host builds: `reg` isn't linked, so
/// this always fails there — matches every other floor module).
pub unsafe fn register_symbols() -> i32 {
    let rows: &[(&[u8], *const c_void)] = &[
        (b"halt\0", pm_metal_rt_halt as *const c_void),
        (b"panic\0", pm_metal_rt_panic as *const c_void),
        (b"panic_at\0", pm_metal_rt_panic_at as *const c_void),
    ];
    for &(func, ptr) in rows {
        if pm_metal_reg_register(MOD.as_ptr(), func.as_ptr(), ptr) != 0 {
            return -1;
        }
    }
    0
}

/// Nothing to bind: `console`/`mem` below are plain `extern "C"` linkage,
/// not a runtime lookup. Kept as a no-op for the stable
/// `pm_metal_rt_connect_symbols` C ABI (see `boot/_impl/_bootstrap.rs`).
pub unsafe fn connect_symbols() -> i32 {
    0
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
pub unsafe fn console_ready() -> i32 {
    pm_metal_console_ready()
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
pub unsafe fn console_ready() -> i32 {
    0
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
pub unsafe fn console_write(id: u32, s: *const u8, n: usize) {
    pm_metal_console_write(id, s, n);
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
pub unsafe fn console_write(_id: u32, _s: *const u8, _n: usize) {}

#[cfg(any(target_os = "none", target_os = "uefi"))]
pub unsafe fn mem_memalign(align: usize, size: usize) -> *mut u8 {
    pm_metal_mem_memalign(align, size)
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
pub unsafe fn mem_free(ptr: *mut u8) {
    pm_metal_mem_free(ptr);
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
pub unsafe fn mem_realloc(ptr: *mut u8, size: usize) -> *mut u8 {
    pm_metal_mem_realloc(ptr, size)
}
