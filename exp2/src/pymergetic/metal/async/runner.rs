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

/// Address of runner `i` control block (stackless — no C stack). 0 if bad.
#[no_mangle]
pub extern "C" fn pm_metal_async_runner_addr(i: u32) -> usize {
    engine::runner_addr(i)
}

/// Queue depths for runner `i` (HIGH/MED/LOW). Returns 0 ok, -1 bad index.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_async_runner_qlen(
    i: u32,
    high: *mut u32,
    med: *mut u32,
    low: *mut u32,
) -> i32 {
    if high.is_null() || med.is_null() || low.is_null() {
        return -1;
    }
    let mut h = 0u32;
    let mut m = 0u32;
    let mut l = 0u32;
    let rc = engine::runner_qlen(i, &mut h, &mut m, &mut l);
    if rc == 0 {
        *high = h;
        *med = m;
        *low = l;
    }
    rc
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
