//! mem.lock — thin RS face over C ABI (`pm_metal_mem_lock_*`).
//! Muscle: `mutex.rs` / `spin.rs` (linked when the mem.lock crate joins product).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

extern "C" {
    fn pm_metal_mem_lock_mutex_init(m: *mut c_void);
    fn pm_metal_mem_lock_mutex_lock(m: *const c_void);
    fn pm_metal_mem_lock_mutex_try_lock(m: *const c_void) -> i32;
    fn pm_metal_mem_lock_mutex_unlock(m: *const c_void);
    fn pm_metal_mem_lock_spin_init(s: *mut c_void);
    fn pm_metal_mem_lock_spin_lock(s: *const c_void);
    fn pm_metal_mem_lock_spin_try_lock(s: *const c_void) -> i32;
    fn pm_metal_mem_lock_spin_unlock(s: *const c_void);
}

#[inline]
pub unsafe fn mutex_init(m: *mut c_void) {
    pm_metal_mem_lock_mutex_init(m)
}
#[inline]
pub unsafe fn mutex_lock(m: *const c_void) {
    pm_metal_mem_lock_mutex_lock(m)
}
#[inline]
pub unsafe fn mutex_try_lock(m: *const c_void) -> i32 {
    pm_metal_mem_lock_mutex_try_lock(m)
}
#[inline]
pub unsafe fn mutex_unlock(m: *const c_void) {
    pm_metal_mem_lock_mutex_unlock(m)
}
#[inline]
pub unsafe fn spin_init(s: *mut c_void) {
    pm_metal_mem_lock_spin_init(s)
}
#[inline]
pub unsafe fn spin_lock(s: *const c_void) {
    pm_metal_mem_lock_spin_lock(s)
}
#[inline]
pub unsafe fn spin_try_lock(s: *const c_void) -> i32 {
    pm_metal_mem_lock_spin_try_lock(s)
}
#[inline]
pub unsafe fn spin_unlock(s: *const c_void) {
    pm_metal_mem_lock_spin_unlock(s)
}
