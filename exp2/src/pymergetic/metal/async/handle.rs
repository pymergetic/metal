//! Async handles + status codes (C ABI).

use crate::engine::{self, Handle, Status};

/// Invalid / unset handle.
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
pub extern "C" fn pm_metal_async_status(h: Handle) -> pm_metal_async_status_t {
    engine::status_of(h).into()
}
