//! Time awaitables — TSC-based mono_us + sleep_us / sleep_until_us.

use crate::engine::{self, Handle};
use crate::handle::pm_metal_async_status_t;

/// Rough QEMU/KVM-ish cycles per microsecond (best-effort until calibrated).
const CYCLES_PER_US: u64 = 2000;

#[repr(C)]
struct SleepFrame {
    deadline_us: u64,
}

fn rdtsc() -> u64 {
    #[cfg(target_arch = "x86_64")]
    {
        let mut lo: u32;
        let mut hi: u32;
        unsafe {
            core::arch::asm!("rdtsc", out("eax") lo, out("edx") hi, options(nostack, nomem));
        }
        ((hi as u64) << 32) | (lo as u64)
    }
    #[cfg(not(target_arch = "x86_64"))]
    {
        0
    }
}

/// Best-effort monotonic microseconds from TSC.
#[no_mangle]
pub extern "C" fn pm_metal_async_mono_us() -> u64 {
    rdtsc() / CYCLES_PER_US
}

unsafe extern "C" fn sleep_step(self_h: Handle) -> u32 {
    let p = engine::coro_state(self_h) as *mut SleepFrame;
    if p.is_null() {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR as u32;
    }
    let now = pm_metal_async_mono_us();
    if now >= (*p).deadline_us {
        pm_metal_async_status_t::PM_METAL_ASYNC_DONE as u32
    } else {
        /* Coop: stay runnable so poll keeps checking the deadline. */
        pm_metal_async_status_t::PM_METAL_ASYNC_PENDING as u32
    }
}

/// Awaitable sleep until absolute mono deadline. Handle is already a MED task.
#[no_mangle]
pub extern "C" fn pm_metal_async_sleep_until_us(deadline_us: u64) -> Handle {
    let h = engine::spawn(
        sleep_step,
        core::mem::size_of::<SleepFrame>() as u32,
        engine::Prio::Med,
    );
    if h == engine::INVALID {
        return engine::INVALID;
    }
    let p = engine::coro_state(h) as *mut SleepFrame;
    if p.is_null() {
        engine::coro_close(h);
        return engine::INVALID;
    }
    unsafe {
        (*p).deadline_us = deadline_us;
    }
    h
}

/// Awaitable relative sleep.
#[no_mangle]
pub extern "C" fn pm_metal_async_sleep_us(us: u64) -> Handle {
    let now = pm_metal_async_mono_us();
    pm_metal_async_sleep_until_us(now.saturating_add(us))
}
