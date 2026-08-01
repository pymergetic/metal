//! modtime — ticks_us / ticks_ms / ticks_diff / time (Metal mono clock).

use crate::upy::py::obj::MpObj;
use crate::upy::py::objects::objint;

extern "C" {
    fn pm_metal_async_mono_us() -> u64;
    fn pm_metal_py_sleep_us(us: u64) -> u32;
}

pub unsafe fn ticks_us() -> u64 {
    pm_metal_async_mono_us()
}

pub unsafe fn ticks_ms() -> u64 {
    pm_metal_async_mono_us() / 1000
}

pub fn ticks_diff(t1: u64, t0: u64) -> i64 {
    // wrapping 30-bit style for small periods; use i64 for host
    t1.wrapping_sub(t0) as i64
}

pub unsafe fn time() -> MpObj {
    // Seconds since boot (mono), not wall clock.
    objint::from_isize((pm_metal_async_mono_us() / 1_000_000) as isize)
}

/// Async sleep via Metal py bridge (returns await handle).
pub unsafe fn sleep_ms(ms: u64) -> u32 {
    pm_metal_py_sleep_us(ms.saturating_mul(1000))
}

pub unsafe fn sleep_us(us: u64) -> u32 {
    pm_metal_py_sleep_us(us)
}
