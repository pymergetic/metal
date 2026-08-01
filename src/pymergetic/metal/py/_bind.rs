//! Publish py edge entrypoints onto `reg` under full module names.

use core::ffi::c_void;

use pymergetic_metal_reg::register_rows_bytes;

use crate::{
    pm_metal_py_alloc, pm_metal_py_await, pm_metal_py_free, pm_metal_py_gc_collect,
    pm_metal_py_gc_enabled, pm_metal_py_proof_await, pm_metal_py_proof_print, pm_metal_py_ready,
    pm_metal_py_sleep_us,
};

const MOD: &[u8] = b"pymergetic.metal.py\0";

/// Register the finished py edge symbols. Returns 0 or -1.
pub unsafe fn publish() -> i32 {
    register_rows_bytes(
        MOD,
        &[
            (b"ready\0", pm_metal_py_ready as *const c_void),
            (b"alloc\0", pm_metal_py_alloc as *const c_void),
            (b"free\0", pm_metal_py_free as *const c_void),
            (b"gc_enabled\0", pm_metal_py_gc_enabled as *const c_void),
            (b"gc_collect\0", pm_metal_py_gc_collect as *const c_void),
            (b"await\0", pm_metal_py_await as *const c_void),
            (b"sleep_us\0", pm_metal_py_sleep_us as *const c_void),
            (b"proof_print\0", pm_metal_py_proof_print as *const c_void),
            (b"proof_await\0", pm_metal_py_proof_await as *const c_void),
        ],
    )
}
