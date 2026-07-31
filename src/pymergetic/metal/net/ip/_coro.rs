//! Shared coro helpers for net protocol crates (sleep, frames, await).
use core::ffi::c_char;

pub use crate::pm_metal_net_ip_poll;

pub const PENDING: u32 = 0;
pub const WAITING: u32 = 1;
pub const DONE: u32 = 2;
pub const CANCELLED: u32 = 3;
pub const ERROR: u32 = 4;

pub type StepFn = unsafe extern "C" fn(u32) -> u32;

extern "C" {
    pub fn pm_metal_async_coro_create(step: Option<StepFn>, state_bytes: u32) -> u32;
    pub fn pm_metal_async_coro_state(h: u32) -> *mut u8;
    pub fn pm_metal_async_coro_close(h: u32);
    pub fn pm_metal_async_await(self_h: u32, child_h: u32) -> u32;
    pub fn pm_metal_async_result_u32(h: u32) -> u32;
    pub fn pm_metal_async_set_result_u32(h: u32, v: u32);
    pub fn pm_metal_async_sleep_us(us: u64) -> u32;
    pub fn pm_metal_time_mono_us() -> u64;
}

pub enum Child {
    Waiting,
    Done(u32),
    Failed,
}

pub unsafe fn finish_child(self_h: u32, child_h: &mut u32) -> Child {
    let h = *child_h;
    let status = pm_metal_async_await(self_h, h);
    if status == WAITING {
        return Child::Waiting;
    }
    let result = pm_metal_async_result_u32(h);
    pm_metal_async_coro_close(h);
    *child_h = 0;
    if status == DONE {
        Child::Done(result)
    } else {
        Child::Failed
    }
}

pub unsafe fn start_sleep(self_h: u32, child_h: &mut u32, us: u64) -> Option<u32> {
    let h = pm_metal_async_sleep_us(us);
    if h == 0 {
        return None;
    }
    *child_h = h;
    Some(pm_metal_async_await(self_h, h))
}

pub unsafe fn cstr_len(s: *const c_char, max: usize) -> usize {
    let mut n = 0;
    while n < max && *s.add(n) != 0 {
        n += 1;
    }
    n
}

pub unsafe fn copy_cstr(dst: &mut [u8], src: *const c_char) -> bool {
    if src.is_null() || dst.is_empty() {
        return false;
    }
    let n = cstr_len(src, dst.len());
    if n >= dst.len() {
        return false;
    }
    for i in 0..n {
        dst[i] = *src.add(i) as u8;
    }
    dst[n] = 0;
    true
}

pub unsafe fn frame<T>(h: u32) -> *mut T {
    pm_metal_async_coro_state(h) as *mut T
}

pub unsafe fn coro_with_frame<T>(step: StepFn) -> u32 {
    let h = pm_metal_async_coro_create(Some(step), core::mem::size_of::<T>() as u32);
    if h == 0 {
        return 0;
    }
    if frame::<T>(h).is_null() {
        pm_metal_async_coro_close(h);
        return 0;
    }
    h
}
