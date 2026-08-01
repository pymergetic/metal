//! task — thin wrapper around a Metal async handle.

use super::core;

#[derive(Clone, Copy)]
pub struct Task {
    pub handle: u32,
}

impl Task {
    pub const fn from_handle(handle: u32) -> Self {
        Self { handle }
    }

    pub unsafe fn done(self) -> bool {
        core::is_done(self.handle)
    }

    pub unsafe fn status(self) -> u32 {
        core::status(self.handle)
    }

    /// Close the Metal coro (cancel-ish).
    pub unsafe fn cancel(self) {
        core::close(self.handle);
    }

    pub unsafe fn await_run(self) -> bool {
        core::run_until(self.handle)
    }
}
