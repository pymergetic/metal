//! SMP waiting mutex — finished for multi-CPU Metal (no OS threads).
//!
//! Futex-shaped state:
//! - `0` unlocked
//! - `1` locked, no waiters observed
//! - `2` locked, waiters may be waiting
//!
//! Contended waiters pause until another CPU unlocks (release store).
//! Same-CPU re-lock while held deadlocks (not reentrant).

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::sync::atomic::{AtomicU32, Ordering};

const UNLOCKED: u32 = 0;
const LOCKED: u32 = 1;
const CONTESTED: u32 = 2;

/// Waiting mutex (caller owns the protected data).
#[repr(C)]
pub struct Mutex {
    state: AtomicU32,
}

impl Mutex {
    pub const fn new() -> Self {
        Self {
            state: AtomicU32::new(UNLOCKED),
        }
    }

    pub fn init(&self) {
        self.state.store(UNLOCKED, Ordering::Release);
    }

    pub fn try_lock(&self) -> bool {
        self.state
            .compare_exchange(UNLOCKED, LOCKED, Ordering::Acquire, Ordering::Relaxed)
            .is_ok()
    }

    pub fn lock(&self) {
        if self
            .state
            .compare_exchange_weak(UNLOCKED, LOCKED, Ordering::Acquire, Ordering::Relaxed)
            .is_ok()
        {
            return;
        }
        self.lock_contended();
    }

    #[inline(never)]
    fn lock_contended(&self) {
        loop {
            let prev = self.state.swap(CONTESTED, Ordering::Acquire);
            if prev == UNLOCKED {
                return;
            }
            while self.state.load(Ordering::Relaxed) != UNLOCKED {
                core::hint::spin_loop();
            }
        }
    }

    pub fn unlock(&self) {
        self.state.swap(UNLOCKED, Ordering::Release);
    }

    pub fn with_lock<R>(&self, f: impl FnOnce() -> R) -> R {
        self.lock();
        let r = f();
        self.unlock();
        r
    }
}

/// Exclusive ownership of `T` across CPUs.
pub struct MutexCell<T> {
    lock: Mutex,
    data: UnsafeCell<T>,
}

/// RAII guard; unlocks on drop.
pub struct MutexGuard<'a, T> {
    cell: &'a MutexCell<T>,
}

unsafe impl<T: Send> Send for MutexCell<T> {}
unsafe impl<T: Send> Sync for MutexCell<T> {}

impl<T> MutexCell<T> {
    pub const fn new(value: T) -> Self {
        Self {
            lock: Mutex::new(),
            data: UnsafeCell::new(value),
        }
    }

    pub fn lock(&self) -> MutexGuard<'_, T> {
        self.lock.lock();
        MutexGuard { cell: self }
    }

    pub fn try_lock(&self) -> Option<MutexGuard<'_, T>> {
        if self.lock.try_lock() {
            Some(MutexGuard { cell: self })
        } else {
            None
        }
    }

    pub unsafe fn unlock_unguarded(&self) {
        self.lock.unlock();
    }

    pub fn get_mut(&mut self) -> &mut T {
        self.data.get_mut()
    }
}

impl<T> Deref for MutexGuard<'_, T> {
    type Target = T;

    fn deref(&self) -> &T {
        unsafe { &*self.cell.data.get() }
    }
}

impl<T> DerefMut for MutexGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.cell.data.get() }
    }
}

impl<T> Drop for MutexGuard<'_, T> {
    fn drop(&mut self) {
        self.cell.lock.unlock();
    }
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_lock_mutex_init(m: *mut Mutex) {
    if m.is_null() {
        return;
    }
    unsafe {
        *m = Mutex::new();
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_lock_mutex_lock(m: *const Mutex) {
    if m.is_null() {
        return;
    }
    (*m).lock();
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_lock_mutex_try_lock(m: *const Mutex) -> i32 {
    if m.is_null() {
        return 0;
    }
    if (*m).try_lock() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_lock_mutex_unlock(m: *const Mutex) {
    if m.is_null() {
        return;
    }
    (*m).unlock();
}
