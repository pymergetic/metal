//! Publish log border onto `reg`.

use core::ffi::c_void;

use pymergetic_metal_reg::register_rows_bytes;

use crate::{pm_metal_log, pm_metal_log_ready};

const MOD: &[u8] = b"pymergetic.metal.log\0";

/// Register finished log symbols. Returns 0 or -1.
pub unsafe fn publish() -> i32 {
    register_rows_bytes(
        MOD,
        &[
            (b"ready\0", pm_metal_log_ready as *const c_void),
            (b"log\0", pm_metal_log as *const c_void),
        ],
    )
}
