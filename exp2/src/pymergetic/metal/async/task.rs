//! Schedule coros as tasks (runner queue entry).

use crate::engine::{self, Handle, StepFn};

/// Timing relevancy — HIGH drained more often than MED/LOW (see runner weights).
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_async_prio_t {
    PM_METAL_ASYNC_PRIO_HIGH = 0,
    PM_METAL_ASYNC_PRIO_MED = 1,
    PM_METAL_ASYNC_PRIO_LOW = 2,
}

/// Enqueue an existing coro on a runner (round-robin CPU). Returns 0 or -1.
#[no_mangle]
pub extern "C" fn pm_metal_async_create_task(h: Handle, prio: pm_metal_async_prio_t) -> i32 {
    engine::create_task(h, engine::prio_from_u32(prio as u32))
}

/// Create coro + schedule in one call (parallel workers / roots).
#[no_mangle]
pub extern "C" fn pm_metal_async_spawn(
    step: StepFn,
    state_bytes: u32,
    prio: pm_metal_async_prio_t,
) -> Handle {
    if step as usize == 0 {
        return engine::INVALID;
    }
    engine::spawn(step, state_bytes, engine::prio_from_u32(prio as u32))
}
