//! Await a child coro/task (nest). Auto-promotes non-task children onto the parent runner.

use crate::engine::{self, Handle};
use crate::handle::pm_metal_async_status_t;

/// Wire self -> child. Returns WAITING if parked, or child's terminal status if already finished.
#[no_mangle]
pub extern "C" fn pm_metal_async_await(self_h: Handle, child_h: Handle) -> pm_metal_async_status_t {
    engine::await_child(self_h, child_h).into()
}
