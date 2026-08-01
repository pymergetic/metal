//! lock — mutex via Event-style wait on Metal sleeps.

use super::core;

pub struct Lock {
    locked: bool,
}

impl Lock {
    pub const fn new() -> Self {
        Self { locked: false }
    }

    pub fn locked(&self) -> bool {
        self.locked
    }

    pub unsafe fn acquire(&mut self) -> bool {
        if !core::ensure_started() {
            return false;
        }
        while self.locked {
            let Some(t) = core::sleep_us(100) else {
                return false;
            };
            if !core::run_until(t.handle) {
                t.cancel();
                return false;
            }
            t.cancel();
        }
        self.locked = true;
        true
    }

    pub fn release(&mut self) -> bool {
        if !self.locked {
            return false;
        }
        self.locked = false;
        true
    }
}
