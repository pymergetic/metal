//! Tiny SMP test-and-set spinlock shared by the static per-module registry
//! core (`_kernel.rs`) and the dynamic/late registration table (`_table.rs`).

use core::sync::atomic::{AtomicU32, Ordering};

pub struct Spin {
    state: AtomicU32,
}

impl Spin {
    pub const fn new() -> Self {
        Self {
            state: AtomicU32::new(0),
        }
    }

    pub fn lock(&self) {
        while self
            .state
            .compare_exchange_weak(0, 1, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
    }

    pub fn unlock(&self) {
        self.state.store(0, Ordering::Release);
    }
}
