//! Global safepoint — pause every runner between coroutine steps so the
//! registry can mutate its module table with zero concurrent access
//! instead of per-call refcounting/locking on the hot path.
//!
//! Caller sequence (see `reg::_kernel::unload`): [`pm_metal_async_quiesce_request`],
//! poll [`pm_metal_async_quiesce_all_parked`] until `1`, do the exclusive
//! work, [`pm_metal_async_quiesce_release`]. A runner parks at its
//! existing per-dispatch checkpoint (`take_ready`, already locked on
//! every task dispatch) -- no new lock, no extra cost on the call path
//! that doesn't request a quiesce.

use crate::engine;

/// Ask every runner to park at its next dispatch checkpoint. Idempotent.
#[no_mangle]
pub extern "C" fn pm_metal_async_quiesce_request() {
    engine::request_quiesce();
}

/// `1` once every started runner has parked since the last request, `0`
/// otherwise. Poll this (spin) before touching whatever the quiesce is
/// protecting.
#[no_mangle]
pub extern "C" fn pm_metal_async_quiesce_all_parked() -> i32 {
    if engine::all_parked() {
        1
    } else {
        0
    }
}

/// Resume every parked runner.
#[no_mangle]
pub extern "C" fn pm_metal_async_quiesce_release() {
    engine::release_quiesce();
}
