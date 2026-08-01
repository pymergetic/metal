//! One exported symbol slot inside a module's static registry state.

use core::ffi::c_void;
use core::sync::atomic::{AtomicPtr, AtomicU32, Ordering};

/// One exported symbol slot inside a module's [`crate::RegModStatic`].
/// `name` is the short function name (no module prefix -- the module
/// name comes from the owning [`crate::RegMod`]). `ptr` is null until
/// `register_symbols` publishes it via [`RegEntry::publish`], and is
/// zeroed again by `deregister_symbols` on unload via
/// [`RegEntry::withdraw`].
///
/// `refs` only matters for `unloadable` providers: incremented by
/// [`crate::acquire`] while a caller holds the resolved pointer,
/// decremented by [`crate::release`] once the call returns. Unload
/// requires every entry's `refs` to have dropped back to `0` first (see
/// `_kernel::unload`) -- a live caller keeps its provider alive.
pub struct RegEntry {
    pub name: &'static str,
    ptr: AtomicPtr<c_void>,
    refs: AtomicU32,
}

// Safety: all fields are atomics; the struct itself carries no interior
// mutability outside them.
unsafe impl Sync for RegEntry {}

impl RegEntry {
    pub const fn new(name: &'static str) -> Self {
        Self {
            name,
            ptr: AtomicPtr::new(core::ptr::null_mut()),
            refs: AtomicU32::new(0),
        }
    }

    pub fn publish(&self, ptr: *const c_void) {
        self.ptr.store(ptr as *mut c_void, Ordering::Release);
    }

    pub fn withdraw(&self) {
        self.ptr.store(core::ptr::null_mut(), Ordering::Release);
    }

    pub fn get(&self) -> *const c_void {
        self.ptr.load(Ordering::Acquire)
    }

    pub fn refs(&self) -> u32 {
        self.refs.load(Ordering::Acquire)
    }

    pub(crate) fn acquire(&self) -> *const c_void {
        let p = self.get();
        if !p.is_null() {
            self.refs.fetch_add(1, Ordering::AcqRel);
        }
        p
    }

    pub(crate) fn release(&self) {
        self.refs.fetch_sub(1, Ordering::AcqRel);
    }
}
