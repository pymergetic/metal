//! One exported symbol slot ([`RegExport`]) inside a module's registry state.

use core::ffi::c_void;
use core::sync::atomic::{AtomicPtr, Ordering};

/// One export: short function name + atomic fn pointer.
///
/// `name` is the short function name (no module prefix — the module name
/// comes from the owning [`crate::RegMod`]). `ptr` is null until
/// `register_symbols` publishes it via [`RegExport::publish`], and is
/// zeroed again by `deregister_symbols` on unload via
/// [`RegExport::withdraw`].
///
/// No refcount: unload quiesces every async runner first (see
/// `pymergetic_metal_async::quiesce`), so there is no concurrent caller
/// while `withdraw` runs.
pub struct RegExport {
    pub name: &'static str,
    ptr: AtomicPtr<c_void>,
}

/// Temporary alias while call sites migrate off the old name.
#[deprecated(note = "renamed to RegExport")]
pub type RegEntry = RegExport;

// Safety: the only field with interior mutability is the atomic.
unsafe impl Sync for RegExport {}

impl RegExport {
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

    pub fn is_published(&self) -> bool {
        !self.get().is_null()
    }
}
