//! Publish console border onto `reg`.

use core::ffi::c_void;

use pymergetic_metal_reg::register_rows_bytes;

use crate::pm_metal_console_ready;

const MOD: &[u8] = b"pymergetic.metal.console\0";

/// Register finished console symbols. Returns 0 or -1.
pub unsafe fn publish() -> i32 {
    register_rows_bytes(MOD, &[(b"ready\0", pm_metal_console_ready as *const c_void)])
}
