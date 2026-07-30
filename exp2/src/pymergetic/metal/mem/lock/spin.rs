//! Busy-wait spinlock — not reentrant, not fair (same tradeoffs as product
//! `runtime/slot/spin.h`). Safe across CPUs in one address space.

use core::sync::atomic::{AtomicU32, Ordering};

/// Exclusive busy-wait lock (`0` free, `1` held).
#[repr(C)]
pub struct Spin {
    state: AtomicU32,
}

impl Spin {
    pub const fn new() -> Self {
        Self {
            state: AtomicU32::new(0),
        }
    }

    pub fn init(&self) {
        self.state.store(0, Ordering::Release);
    }

    /// Spin until the lock is acquired.
    pub fn lock(&self) {
        while self
            .state
            .compare_exchange_weak(0, 1, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
    }

    /// Try once; `true` if acquired.
    pub fn try_lock(&self) -> bool {
        self.state
            .compare_exchange(0, 1, Ordering::Acquire, Ordering::Relaxed)
            .is_ok()
    }

    pub fn unlock(&self) {
        self.state.store(0, Ordering::Release);
    }

    pub fn with_lock<R>(&self, f: impl FnOnce() -> R) -> R {
        self.lock();
        let r = f();
        self.unlock();
        r
    }
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_lock_spin_init(s: *mut Spin) {
    if s.is_null() {
        return;
    }
    unsafe {
        *s = Spin::new();
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_lock_spin_lock(s: *const Spin) {
    if s.is_null() {
        return;
    }
    (*s).lock();
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_lock_spin_try_lock(s: *const Spin) -> i32 {
    if s.is_null() {
        return 0;
    }
    if (*s).try_lock() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_lock_spin_unlock(s: *const Spin) {
    if s.is_null() {
        return;
    }
    (*s).unlock();
}
