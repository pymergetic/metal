//! `rt` cross-module reach without a Cargo cycle:
//!
//! - `console` / `mem`: product C callees (`extern "C"`, link-time).
//! - Registry (`pm_metal_reg_register`): optional; product LIVE does not link
//!   `reg` yet, so `register_symbols` returns -1 until that crate is wired.

use core::ffi::c_void;

use crate::{pm_metal_rt_halt, pm_metal_rt_panic, pm_metal_rt_panic_at};

const MOD: &[u8] = b"pymergetic.metal.rt\0";

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_console_ready() -> i32;
    /* Product ABI: ring write, no console-id (see console/__init__.c). */
    fn pm_metal_console_write(data: *const u8, n: usize) -> usize;
    fn pm_metal_mem_memalign(align: usize, size: usize) -> *mut u8;
    fn pm_metal_mem_free(ptr: *mut u8);
    fn pm_metal_mem_realloc(ptr: *mut u8, size: usize) -> *mut u8;
}

#[cfg(feature = "reg")]
#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_reg_register(full_module: *const u8, func: *const u8, ptr: *const c_void) -> i32;
}

/// Register rt's dynamically-callable exports. Without `reg` feature: -1.
pub unsafe fn register_symbols() -> i32 {
    #[cfg(all(feature = "reg", any(target_os = "none", target_os = "uefi")))]
    {
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
        return 0;
    }
    #[cfg(not(all(feature = "reg", any(target_os = "none", target_os = "uefi"))))]
    {
        let _ = (
            MOD,
            pm_metal_rt_halt as *const c_void,
            pm_metal_rt_panic as *const c_void,
            pm_metal_rt_panic_at as *const c_void,
        );
        -1
    }
}

/// Nothing to bind: console/mem are plain `extern "C"`.
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
pub unsafe fn console_write(_id: u32, s: *const u8, n: usize) {
    let _ = pm_metal_console_write(s, n);
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

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
pub unsafe fn mem_memalign(_align: usize, _size: usize) -> *mut u8 {
    core::ptr::null_mut()
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
pub unsafe fn mem_free(_ptr: *mut u8) {}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
pub unsafe fn mem_realloc(_ptr: *mut u8, _size: usize) -> *mut u8 {
    core::ptr::null_mut()
}
