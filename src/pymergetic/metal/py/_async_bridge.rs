//! Py await = Metal async (Locked #5). Thin bridge only — no upy scheduler.

#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Ready = 0,
    Waiting = 1,
    Done = 2,
    Cancelled = 3,
    Error = 4,
}

extern "C" {
    fn pm_metal_async_await(self_h: u32, child_h: u32) -> u32;
    fn pm_metal_async_sleep_us(us: u64) -> u32;
}

pub unsafe fn await_child(self_h: u32, child_h: u32) -> Status {
    match pm_metal_async_await(self_h, child_h) {
        0 => Status::Ready,
        1 => Status::Waiting,
        2 => Status::Done,
        3 => Status::Cancelled,
        _ => Status::Error,
    }
}

pub fn sleep_us(us: u64) -> u32 {
    unsafe { pm_metal_async_sleep_us(us) }
}
