//! One exported symbol slot inside a module's static registry state.

use core::ffi::c_void;
use core::sync::atomic::{AtomicPtr, Ordering};

/// One exported symbol slot inside a module's [`crate::RegModStatic`].
/// `name` is the short function name (no module prefix -- the module
/// name comes from the owning [`crate::RegMod`]). `ptr` is null until
/// `register_symbols` publishes it via [`RegEntry::publish`], and is
/// zeroed again by `deregister_symbols` on unload via
/// [`RegEntry::withdraw`].
///
/// No refcount: a provider is safe to withdraw at any time because
/// `_kernel::unload` quiesces every async runner first (see
/// `pymergetic_metal_async::quiesce`) -- there is no concurrent caller
/// anywhere in the system while `withdraw` runs, fixed provider or
/// unloadable one alike, so there is nothing for a per-entry live-count
/// to protect against.
pub struct RegEntry {
    pub name: &'static str,
    ptr: AtomicPtr<c_void>,
}

// Safety: the only field with interior mutability is the atomic.
unsafe impl Sync for RegEntry {}

impl RegEntry {
    pub const fn new(name: &'static str) -> Self {
        Self {
            name,
            ptr: AtomicPtr::new(core::ptr::null_mut()),
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
}
