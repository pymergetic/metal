//! N equal runners (1/CPU), each with HIGH/MED/LOW queues, weighted drain + steal.

use crate::engine;

/// Start the runner pool once. `n_cpus` clamped to >= 1. Returns 0 or -1.
#[no_mangle]
pub extern "C" fn pm_metal_async_start(n_cpus: u32) -> i32 {
    engine::start(n_cpus)
}

/// 1 if start() has succeeded.
#[no_mangle]
pub extern "C" fn pm_metal_async_ready() -> i32 {
    if engine::started() {
        1
    } else {
        0
    }
}

/// Runner count after start (0 if not started).
#[no_mangle]
pub extern "C" fn pm_metal_async_n_runners() -> u32 {
    engine::n_runners()
}

/// Tune drain weights (defaults 4/2/1). Zero args become 1.
#[no_mangle]
pub extern "C" fn pm_metal_async_set_weights(high: u32, med: u32, low: u32) {
    engine::set_weights(high, med, low);
}

/// Drain current runner: weighted H->M->L, skip empty, steal if idle.
/// Returns number of steps run (>= 0), or -1 if not started.
#[no_mangle]
pub extern "C" fn pm_metal_async_run_poll() -> i32 {
    engine::run_poll()
}

/// Drain every runner once (UP bringup / single-thread host).
#[no_mangle]
pub extern "C" fn pm_metal_async_run_poll_all() -> i32 {
    engine::run_poll_all()
}

/// Poll forever (halt path / main loop). Does not return on success.
#[no_mangle]
pub extern "C" fn pm_metal_async_run_loop() -> ! {
    loop {
        let n = engine::run_poll_all();
        if n <= 0 {
            core::hint::spin_loop();
        }
    }
}
