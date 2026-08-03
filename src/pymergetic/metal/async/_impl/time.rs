//! Time awaitables — calibrated TSC mono_us + sleep_us / sleep_until_us.

use core::sync::atomic::{AtomicU64, Ordering};

use crate::engine;
use crate::handle::pm_metal_async_status_t;

const FALLBACK_CYCLES_PER_US: u64 = 2000;

#[repr(C)]
struct BootTimeOps {
    tsc_per_us: Option<unsafe extern "C" fn() -> u64>,
    invalidate: Option<unsafe extern "C" fn()>,
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_boot_time_ops() -> *const BootTimeOps;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_boot_time_ops() -> *const BootTimeOps {
    core::ptr::null()
}

static TSC_PER_US: AtomicU64 = AtomicU64::new(0);

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

fn port_tsc_per_us() -> u64 {
    let ops = unsafe { pm_metal_boot_time_ops() };
    if ops.is_null() {
        return 0;
    }
    let f = unsafe { (*ops).tsc_per_us };
    match f {
        Some(f) => unsafe { f() },
        None => 0,
    }
}

fn port_invalidate() {
    let ops = unsafe { pm_metal_boot_time_ops() };
    if ops.is_null() {
        return;
    }
    if let Some(f) = unsafe { (*ops).invalidate } {
        unsafe { f() };
    }
}

fn ensure_calibrated() -> u64 {
    let cur = TSC_PER_US.load(Ordering::Relaxed);
    if cur != 0 {
        return cur;
    }
    let mut v = port_tsc_per_us();
    if v == 0 {
        v = FALLBACK_CYCLES_PER_US;
    }
    TSC_PER_US.store(v, Ordering::Relaxed);
    v
}

/// Calibrate TSC (BSP). Idempotent. Call before leave_firmware on EFI.
#[no_mangle]
pub extern "C" fn pm_metal_time_init() {
    let _ = ensure_calibrated();
}

/// Drop cache and re-sample (EFI post-EBS returns sticky port cache).
#[no_mangle]
pub extern "C" fn pm_metal_time_recalibrate() {
    port_invalidate();
    TSC_PER_US.store(0, Ordering::Relaxed);
    let _ = ensure_calibrated();
}

/// TSC ticks per microsecond (also ~MHz). 0 only before first init attempt.
#[no_mangle]
pub extern "C" fn pm_metal_time_tsc_per_us() -> u64 {
    ensure_calibrated()
}

/// Best-effort monotonic microseconds from calibrated TSC.
#[no_mangle]
pub extern "C" fn pm_metal_async_mono_us() -> u64 {
    let per = ensure_calibrated();
    if per == 0 {
        return 0;
    }
    rdtsc() / per
}

/// Alias for callers that want the product-shaped name.
#[no_mangle]
pub extern "C" fn pm_metal_time_mono_us() -> u64 {
    pm_metal_async_mono_us()
}

#[repr(C)]
struct SleepFrame {
    deadline_us: u64,
}

unsafe extern "C" fn sleep_step(self_h: u32) -> u32 {
    let p = engine::coro_state(self_h) as *mut SleepFrame;
    if p.is_null() {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR as u32;
    }
    let now = pm_metal_async_mono_us();
    if now >= unsafe { (*p).deadline_us } {
        pm_metal_async_status_t::PM_METAL_ASYNC_DONE as u32
    } else {
        /* Coop: stay runnable so poll keeps checking the deadline. */
        pm_metal_async_status_t::PM_METAL_ASYNC_PENDING as u32
    }
}

/// Awaitable sleep until absolute mono deadline. Handle is already a MED task.
#[no_mangle]
pub extern "C" fn pm_metal_async_sleep_until_us(deadline_us: u64) -> u32 {
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
pub extern "C" fn pm_metal_async_sleep_us(us: u64) -> u32 {
    let now = pm_metal_async_mono_us();
    pm_metal_async_sleep_until_us(now.saturating_add(us))
}

/// Guest/product alias: sleep milliseconds (awaitable handle).
#[no_mangle]
pub extern "C" fn pm_metal_async_sleep(ms: u32) -> u32 {
    pm_metal_async_sleep_us((ms as u64).saturating_mul(1000))
}

/// Guest/product alias: yield one coop slice (sleep 0).
#[no_mangle]
pub extern "C" fn pm_metal_async_yield() -> u32 {
    pm_metal_async_sleep_us(0)
}

/// Monotonic milliseconds (mono_us / 1000).
#[no_mangle]
pub extern "C" fn pm_metal_async_mono_ms() -> u64 {
    pm_metal_async_mono_us() / 1000
}
