//! Schedule coros as tasks (runner queue entry).

use crate::coro::pm_metal_async_step_fn_t;
use crate::engine::{self, StepFn};


/// Timing relevancy — HIGH drained more often than MED/LOW (see runner weights).
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_async_prio_t {
    PM_METAL_ASYNC_PRIO_HIGH = 0,
    PM_METAL_ASYNC_PRIO_MED = 1,
    PM_METAL_ASYNC_PRIO_LOW = 2,
}

/// Enqueue an existing coro on a runner at MED (product-shaped; returns `h` or 0).
#[no_mangle]
pub extern "C" fn pm_metal_async_create_task(h: u32) -> u32 {
    if engine::create_task(h, engine::Prio::Med) != 0 {
        engine::INVALID
    } else {
        h
    }
}

/// Enqueue with explicit priority. Returns 0 or -1.
#[no_mangle]
pub extern "C" fn pm_metal_async_create_task_prio(
    h: u32,
    prio: pm_metal_async_prio_t,
) -> i32 {
    engine::create_task(h, engine::prio_from_u32(prio as u32))
}

/// Create coro + schedule in one call (parallel workers / roots).
#[no_mangle]
pub extern "C" fn pm_metal_async_spawn(
    step: pm_metal_async_step_fn_t,
    state_bytes: u32,
    prio: pm_metal_async_prio_t,
) -> u32 {
    let Some(step) = step else {
        return engine::INVALID;
    };
    engine::spawn(step as StepFn, state_bytes, engine::prio_from_u32(prio as u32))
}
