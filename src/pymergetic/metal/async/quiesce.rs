//! Global safepoint flags for registry unload.
//!
//! Handshake with the C N-runner (`async/__init__.c`):
//!   request → clear parked[0..N)
//!   each runner parks at its next dispatch checkpoint via `park_runner(ri)`
//!   all_parked when n_runners==0 (vacuous) OR every ri in [0,n) is parked
//!   release → clear REQUESTED + parked → runners resume

use core::sync::atomic::{AtomicBool, Ordering};

const MAX_RUNNERS: usize = 8;

static REQUESTED: AtomicBool = AtomicBool::new(false);
static PARKED: [AtomicBool; MAX_RUNNERS] = [
    AtomicBool::new(false),
    AtomicBool::new(false),
    AtomicBool::new(false),
    AtomicBool::new(false),
    AtomicBool::new(false),
    AtomicBool::new(false),
    AtomicBool::new(false),
    AtomicBool::new(false),
];

/// Ask every runner to park at its next dispatch checkpoint. Idempotent.
#[no_mangle]
pub extern "C" fn pm_metal_async_quiesce_request() {
    REQUESTED.store(true, Ordering::SeqCst);
    for p in PARKED.iter() {
        p.store(false, Ordering::SeqCst);
    }
}

/// `1` while a quiesce request is outstanding.
#[no_mangle]
pub extern "C" fn pm_metal_async_quiesce_requested() -> i32 {
    if REQUESTED.load(Ordering::SeqCst) {
        1
    } else {
        0
    }
}

/// Checkpoint ack from C: runner `ri` has parked (no further dispatch).
#[no_mangle]
pub extern "C" fn pm_metal_async_quiesce_park_runner(ri: u32) {
    if (ri as usize) < MAX_RUNNERS && REQUESTED.load(Ordering::SeqCst) {
        PARKED[ri as usize].store(true, Ordering::SeqCst);
    }
}

/// `1` once every started runner has parked since the last request, `0` otherwise.
#[no_mangle]
pub extern "C" fn pm_metal_async_quiesce_all_parked() -> i32 {
    if !REQUESTED.load(Ordering::SeqCst) {
        return 0;
    }
    extern "C" {
        fn pm_metal_async_n_runners() -> u32;
    }
    let n = unsafe { pm_metal_async_n_runners() } as usize;
    if n == 0 {
        return 1;
    }
    let n = if n > MAX_RUNNERS { MAX_RUNNERS } else { n };
    for i in 0..n {
        if !PARKED[i].load(Ordering::SeqCst) {
            return 0;
        }
    }
    1
}

/// Resume every parked runner.
#[no_mangle]
pub extern "C" fn pm_metal_async_quiesce_release() {
    REQUESTED.store(false, Ordering::SeqCst);
    for p in PARKED.iter() {
        p.store(false, Ordering::SeqCst);
    }
}
