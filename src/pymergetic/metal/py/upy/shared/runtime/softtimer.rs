//! softtimer — SHARED_OPT → Metal async sleep (no second timer wheel).

use crate::upy::extmod::asyncio::{run_until, sleep_us, Task};

/// One-shot delay; returns Metal task handle wrapper.
pub unsafe fn after_us(us: u64) -> Option<Task> {
    sleep_us(us)
}

pub unsafe fn after_ms(ms: u64) -> Option<Task> {
    after_us(ms.saturating_mul(1000))
}

/// Block (coop poll) until the softtimer fires.
pub unsafe fn wait(t: Task) -> bool {
    run_until(t.handle)
}
