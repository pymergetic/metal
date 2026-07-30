//! Process = task crowned with an orchestration id (same activation).

use crate::engine::{self, Handle};
use crate::handle::pm_metal_async_status_t;

/// Stable process id (0 = none / failure).
pub type pm_metal_async_pid_t = u32;

/// Crown a scheduled task as a process root. Returns pid or 0.
#[no_mangle]
pub extern "C" fn pm_metal_async_process_crown(task_h: Handle) -> pm_metal_async_pid_t {
    engine::process_crown(task_h)
}

/// Task handle for `pid`, or 0.
#[no_mangle]
pub extern "C" fn pm_metal_async_process_handle(pid: pm_metal_async_pid_t) -> Handle {
    engine::process_handle(pid)
}

/// Cancel process (marks CANCELLED + wakes waiter). Returns 0 or -1.
#[no_mangle]
pub extern "C" fn pm_metal_async_process_kill(pid: pm_metal_async_pid_t) -> i32 {
    engine::process_kill(pid)
}

/// Await the process task (same as await on its handle).
#[no_mangle]
pub extern "C" fn pm_metal_async_process_wait(
    self_h: Handle,
    pid: pm_metal_async_pid_t,
) -> pm_metal_async_status_t {
    let h = engine::process_handle(pid);
    if h == engine::INVALID {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR;
    }
    engine::await_child(self_h, h).into()
}
