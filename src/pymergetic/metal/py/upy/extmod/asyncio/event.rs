//! event — Event flag; wait parks via Metal sleep slices (no upy scheduler).

use super::core;

pub struct Event {
    set: bool,
}

impl Event {
    pub const fn new() -> Self {
        Self { set: false }
    }

    pub fn is_set(&self) -> bool {
        self.set
    }

    pub fn set(&mut self) {
        self.set = true;
    }

    pub fn clear(&mut self) {
        self.set = false;
    }

    /// Wait until set: short Metal sleeps + poll (async-first, no busy spin without yield).
    pub unsafe fn wait(&mut self) -> bool {
        if !core::ensure_started() {
            return false;
        }
        while !self.set {
            let Some(t) = core::sleep_us(100) else {
                return false;
            };
            if !core::run_until(t.handle) {
                t.cancel();
                return false;
            }
            t.cancel();
        }
        true
    }
}
