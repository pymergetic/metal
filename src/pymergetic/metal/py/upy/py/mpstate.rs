//! mpstate — single global VM state (no isolation / no percpu cages).

use core::sync::atomic::{AtomicBool, Ordering};

use super::qstr;

/// Process-wide upy state. One instance only (Locked #3/#4).
pub struct MpState {
    pub ready: AtomicBool,
}

impl MpState {
    const fn new() -> Self {
        Self {
            ready: AtomicBool::new(false),
        }
    }
}

static STATE: MpState = MpState::new();

/// Initialize qstr pool + mark state ready.
pub fn init() {
    qstr::init();
    STATE.ready.store(true, Ordering::Release);
}

pub fn ready() -> bool {
    STATE.ready.load(Ordering::Acquire)
}

/// Access the sole state object.
pub fn state() -> &'static MpState {
    &STATE
}
