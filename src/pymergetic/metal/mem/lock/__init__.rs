//! Package marker for mem.lock — C ABI is sibling stems `spin` / `mutex`
//! (no umbrella re-export). Rust `pub use` below is crate-internal wiring only.
//!
//! - [`spin`]: busy-wait exclusive (+ C `pm_metal_mem_lock_spin_*`)
//! - [`mutex`]: SMP waiting mutex (+ C `pm_metal_mem_lock_mutex_*`); [`MutexCell`] owns data

#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, unused_imports)]

mod mutex;
mod spin;

pub use mutex::{Mutex, MutexCell, MutexGuard};
pub use spin::Spin;


use core::ffi::c_void;

use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

pymergetic_metal_reg::reg_mod! {
    mod lock = "pymergetic.metal.mem.lock";
    exports: [mutex_init, mutex_lock, mutex_try_lock, mutex_unlock, spin_init, spin_lock, spin_try_lock, spin_unlock];
}

extern "C" fn lock_register_symbols(_ctx: *mut c_void) -> i32 {
    use mutex::{
        pm_metal_mem_lock_mutex_init, pm_metal_mem_lock_mutex_lock, pm_metal_mem_lock_mutex_try_lock,
        pm_metal_mem_lock_mutex_unlock,
    };
    use spin::{
        pm_metal_mem_lock_spin_init, pm_metal_mem_lock_spin_lock, pm_metal_mem_lock_spin_try_lock,
        pm_metal_mem_lock_spin_unlock,
    };
    lock::mutex_init.publish(pm_metal_mem_lock_mutex_init as *const c_void);
    lock::mutex_lock.publish(pm_metal_mem_lock_mutex_lock as *const c_void);
    lock::mutex_try_lock.publish(pm_metal_mem_lock_mutex_try_lock as *const c_void);
    lock::mutex_unlock.publish(pm_metal_mem_lock_mutex_unlock as *const c_void);
    lock::spin_init.publish(pm_metal_mem_lock_spin_init as *const c_void);
    lock::spin_lock.publish(pm_metal_mem_lock_spin_lock as *const c_void);
    lock::spin_try_lock.publish(pm_metal_mem_lock_spin_try_lock as *const c_void);
    lock::spin_unlock.publish(pm_metal_mem_lock_spin_unlock as *const c_void);
    0
}

static LOCK_MOD: RegMod = RegMod::from_static(
    lock::NAME,
    &lock::STORAGE.exports,
    &lock::STORAGE.imports,
    Some(lock_register_symbols),
);

#[no_mangle]
pub extern "C" fn pm_metal_mem_lock_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod(lock::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&LOCK_MOD) }
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_mem_lock_reg_load()
}
