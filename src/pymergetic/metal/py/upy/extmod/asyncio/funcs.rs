//! funcs — wait_for_ms / gather on Metal handles.

use super::core;
use super::task::Task;

/// Wait for `h` or timeout (ms). Ok(true)=done, Ok(false)=timeout, Err=fail.
pub unsafe fn wait_for_ms(h: u32, timeout_ms: u64) -> Result<bool, ()> {
    if !core::ensure_started() || h == core::INVALID {
        return Err(());
    }
    let deadline = {
        extern "C" {
            fn pm_metal_async_mono_us() -> u64;
        }
        pm_metal_async_mono_us().saturating_add(timeout_ms.saturating_mul(1000))
    };
    loop {
        if core::is_done(h) {
            return Ok(core::status(h) == core::STATUS_DONE);
        }
        extern "C" {
            fn pm_metal_async_mono_us() -> u64;
        }
        if pm_metal_async_mono_us() >= deadline {
            return Ok(false);
        }
        core::poll_once();
        // yield a little so sleep tasks advance
        if let Some(t) = core::sleep_us(50) {
            let _ = core::run_until(t.handle);
            t.cancel();
        }
    }
}

/// Wait until every handle is terminal.
pub unsafe fn gather(handles: &[u32]) -> bool {
    if !core::ensure_started() {
        return false;
    }
    for &h in handles {
        if h == core::INVALID {
            return false;
        }
    }
    loop {
        let mut all = true;
        for &h in handles {
            if !core::is_done(h) {
                all = false;
                break;
            }
        }
        if all {
            return handles
                .iter()
                .all(|&h| core::status(h) == core::STATUS_DONE);
        }
        core::poll_once();
        if let Some(t) = core::sleep_us(50) {
            let _ = core::run_until(t.handle);
            t.cancel();
        }
    }
}

pub unsafe fn gather_tasks(tasks: &[Task]) -> bool {
    let mut hs = [0u32; 8];
    if tasks.len() > hs.len() {
        return false;
    }
    for (i, t) in tasks.iter().enumerate() {
        hs[i] = t.handle;
    }
    gather(&hs[..tasks.len()])
}
