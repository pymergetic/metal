//! mem.port — thin RS face over C heap ABI.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
use core::ffi::c_void;
extern "C" {
    fn pm_metal_mem_init(base: *mut c_void, bytes: usize) -> i32;
    fn pm_metal_mem_alloc(bytes: usize) -> *mut c_void;
    fn pm_metal_mem_free(ptr: *mut c_void);
    fn pm_metal_mem_free_bytes() -> usize;
}
#[inline] pub unsafe fn init(base: *mut c_void, bytes: usize) -> i32 { pm_metal_mem_init(base, bytes) }
#[inline] pub unsafe fn alloc(bytes: usize) -> *mut c_void { pm_metal_mem_alloc(bytes) }
#[inline] pub unsafe fn free(ptr: *mut c_void) { pm_metal_mem_free(ptr) }
#[inline] pub fn free_bytes() -> usize { unsafe { pm_metal_mem_free_bytes() } }
