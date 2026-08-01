//! core — sleep / create_task / run_until on Metal async handles.

use super::task::Task;

extern "C" {
    fn pm_metal_async_start(n_cpus: u32) -> i32;
    fn pm_metal_async_ready() -> i32;
    fn pm_metal_async_run_poll_all() -> i32;
    fn pm_metal_async_sleep_us(us: u64) -> u32;
    fn pm_metal_async_status(h: u32) -> u32;
    fn pm_metal_async_create_task(h: u32) -> u32;
    fn pm_metal_async_coro_close(h: u32);
}

pub const STATUS_PENDING: u32 = 0;
pub const STATUS_WAITING: u32 = 1;
pub const STATUS_DONE: u32 = 2;
pub const STATUS_CANCELLED: u32 = 3;
pub const STATUS_ERROR: u32 = 4;
pub const INVALID: u32 = 0;

/// Start Metal runners once (idempotent).
pub unsafe fn ensure_started() -> bool {
    if pm_metal_async_ready() != 0 {
        return true;
    }
    pm_metal_async_start(1) == 0 && pm_metal_async_ready() != 0
}

pub unsafe fn status(h: u32) -> u32 {
    if h == INVALID {
        return STATUS_ERROR;
    }
    pm_metal_async_status(h)
}

pub unsafe fn is_done(h: u32) -> bool {
    matches!(status(h), STATUS_DONE | STATUS_CANCELLED | STATUS_ERROR)
}

/// Relative sleep → Metal awaitable handle (already a MED task).
pub unsafe fn sleep_us(us: u64) -> Option<Task> {
    if !ensure_started() {
        return None;
    }
    let h = pm_metal_async_sleep_us(us);
    if h == INVALID {
        None
    } else {
        Some(Task::from_handle(h))
    }
}

pub unsafe fn sleep_ms(ms: u64) -> Option<Task> {
    sleep_us(ms.saturating_mul(1000))
}

pub unsafe fn sleep(seconds: u64) -> Option<Task> {
    sleep_us(seconds.saturating_mul(1_000_000))
}

/// Ensure handle is on a runner (no-op if sleep already spawned as task).
pub unsafe fn create_task(h: u32) -> Option<Task> {
    if !ensure_started() || h == INVALID {
        return None;
    }
    let out = pm_metal_async_create_task(h);
    if out == INVALID {
        None
    } else {
        Some(Task::from_handle(out))
    }
}

/// Coop poll until handle finishes (host / firmware bring-up).
pub unsafe fn run_until(h: u32) -> bool {
    if !ensure_started() || h == INVALID {
        return false;
    }
    let mut spins = 0u32;
    while !is_done(h) {
        let _ = pm_metal_async_run_poll_all();
        spins = spins.wrapping_add(1);
        if spins > 50_000_000 {
            return false;
        }
    }
    status(h) == STATUS_DONE
}

pub unsafe fn poll_once() {
    if pm_metal_async_ready() != 0 {
        let _ = pm_metal_async_run_poll_all();
    }
}

pub unsafe fn close(h: u32) {
    if h != INVALID {
        pm_metal_async_coro_close(h);
    }
}
