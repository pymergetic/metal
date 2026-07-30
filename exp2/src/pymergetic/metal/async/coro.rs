//! Coro create / durable step frame / close.

use crate::engine::{self, Handle, StepFn};

/// Step entry: park via await/WAITING, else PENDING / DONE / ERROR / CANCELLED.
pub type pm_metal_async_step_fn_t = StepFn;

/// Create a coro with a zeroed durable frame of `state_bytes` (0 = no frame).
/// Not scheduled until create_task / spawn / await-promote.
#[no_mangle]
pub extern "C" fn pm_metal_async_coro_create(step: StepFn, state_bytes: u32) -> Handle {
    if step as usize == 0 {
        return engine::INVALID;
    }
    engine::coro_create(step, state_bytes)
}

/// Pointer to the durable step frame (NULL if none / bad handle).
#[no_mangle]
pub extern "C" fn pm_metal_async_coro_state(h: Handle) -> *mut u8 {
    engine::coro_state(h)
}

/// Release handle + frame. Do not use `h` afterward.
#[no_mangle]
pub extern "C" fn pm_metal_async_coro_close(h: Handle) {
    engine::coro_close(h);
}
