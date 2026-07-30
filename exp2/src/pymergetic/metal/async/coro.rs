//! Coro create / durable step frame / close.

use crate::engine::{self, StepFn};


/// Step entry: park via await/WAITING, else PENDING / DONE / ERROR / CANCELLED.
/// Option<> so metal mod sync emits a C function-pointer typedef.
pub type pm_metal_async_step_fn_t = Option<unsafe extern "C" fn(u32) -> u32>;

/// Create a coro with a zeroed durable frame of `state_bytes` (0 = no frame).
/// Not scheduled until create_task / spawn / await-promote.
#[no_mangle]
pub extern "C" fn pm_metal_async_coro_create(
    step: pm_metal_async_step_fn_t,
    state_bytes: u32,
) -> u32 {
    let Some(step) = step else {
        return engine::INVALID;
    };
    engine::coro_create(step as StepFn, state_bytes)
}

/// Pointer to the durable step frame (NULL if none / bad handle).
#[no_mangle]
pub extern "C" fn pm_metal_async_coro_state(h: u32) -> *mut u8 {
    engine::coro_state(h)
}

/// Release handle + frame. Do not use `h` afterward.
#[no_mangle]
pub extern "C" fn pm_metal_async_coro_close(h: u32) {
    engine::coro_close(h);
}
