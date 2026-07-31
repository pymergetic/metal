//! Async handles + status codes (C ABI).

use crate::engine::{self, Status};

/// Invalid / unset handle (C: uint32_t).
pub const PM_METAL_ASYNC_HANDLE_INVALID: u32 = 0;

/// Cooperative step / await result codes.
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_async_status_t {
    PM_METAL_ASYNC_PENDING = 0,
    PM_METAL_ASYNC_WAITING = 1,
    PM_METAL_ASYNC_DONE = 2,
    PM_METAL_ASYNC_CANCELLED = 3,
    PM_METAL_ASYNC_ERROR = 4,
}

impl From<Status> for pm_metal_async_status_t {
    fn from(s: Status) -> Self {
        match s {
            Status::Pending => pm_metal_async_status_t::PM_METAL_ASYNC_PENDING,
            Status::Waiting => pm_metal_async_status_t::PM_METAL_ASYNC_WAITING,
            Status::Done => pm_metal_async_status_t::PM_METAL_ASYNC_DONE,
            Status::Cancelled => pm_metal_async_status_t::PM_METAL_ASYNC_CANCELLED,
            Status::Error => pm_metal_async_status_t::PM_METAL_ASYNC_ERROR,
        }
    }
}

/// Current status of handle `h` (ERROR if invalid).
#[no_mangle]
pub extern "C" fn pm_metal_async_status(h: u32) -> pm_metal_async_status_t {
    engine::status_of(h).into()
}

/// Store a u32 completion value on handle `h` (net/connect/recv/dns, …).
#[no_mangle]
pub extern "C" fn pm_metal_async_set_result_u32(h: u32, v: u32) {
    engine::set_result_u32(h, v);
}

/// Read the u32 completion value (0 if bad handle).
#[no_mangle]
pub extern "C" fn pm_metal_async_result_u32(h: u32) -> u32 {
    engine::result_u32(h)
}

/// Already-complete handle (`DONE`) carrying `v` — for sync backends on async APIs.
#[no_mangle]
pub extern "C" fn pm_metal_async_completed_u32(v: u32) -> u32 {
    engine::completed_u32(v)
}
