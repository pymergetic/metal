//! Async concurrency metrics (W11.5) — spawns/awaits/per-runner steps/starve gaps.

use crate::engine;

/// Clear counters and per-runner last-step timestamps.
#[no_mangle]
pub extern "C" fn pm_metal_async_metric_reset() {
    engine::metric_reset();
}

/// Successful [`crate::task::pm_metal_async_spawn`] / sleep spawn count since reset.
#[no_mangle]
pub extern "C" fn pm_metal_async_metric_spawns() -> u64 {
    engine::metric_spawns()
}

/// [`crate::r#await::pm_metal_async_await`] call count since reset.
#[no_mangle]
pub extern "C" fn pm_metal_async_metric_awaits() -> u64 {
    engine::metric_awaits()
}

/// Steps executed on runner `i` since reset (0 if bad index / not started).
#[no_mangle]
pub extern "C" fn pm_metal_async_metric_steps(runner: u32) -> u64 {
    engine::metric_steps(runner)
}

/// Max mono gap (us) between successive steps on the same runner since reset.
#[no_mangle]
pub extern "C" fn pm_metal_async_metric_starve_max_us() -> u64 {
    engine::metric_starve_max_us()
}
