//! Import slot + fixed-capacity per-module registry state.

use core::ffi::c_void;
use core::sync::atomic::{AtomicPtr, Ordering};

use crate::entry::RegExport;

/// One import: peer `(module, func)` + cached [`RegExport`] after connect.
///
/// Populated by `_kernel::connect_one` / `connect_all`. Reset to null when
/// the peer was never found or has since been unloaded.
///
/// Hot path: [`RegImport::export`] → [`RegExport::get`] — no strings.
pub struct RegImport {
    pub module: &'static str,
    pub func: &'static str,
    slot: AtomicPtr<RegExport>,
}

/// Temporary alias while call sites migrate off the old name.
#[deprecated(note = "renamed to RegImport")]
pub type ImportRow = RegImport;

// Safety: `slot` is the only field with interior mutability, and it is an
// atomic.
unsafe impl Sync for RegImport {}

impl RegImport {
    pub const fn new(module: &'static str, func: &'static str) -> Self {
        Self {
            module,
            func,
            slot: AtomicPtr::new(core::ptr::null_mut()),
        }
    }

    pub(crate) fn set(&self, export: Option<&'static RegExport>) {
        let p = match export {
            Some(e) => e as *const RegExport as *mut RegExport,
            None => core::ptr::null_mut(),
        };
        self.slot.store(p, Ordering::Release);
    }

    /// Cached export handle after `connect_all`, or None if not hooked.
    pub fn export(&self) -> Option<&'static RegExport> {
        let p = self.slot.load(Ordering::Acquire);
        if p.is_null() {
            None
        } else {
            Some(unsafe { &*p })
        }
    }

    /// Old name — prefer [`RegImport::export`].
    #[inline]
    pub fn entry(&self) -> Option<&'static RegExport> {
        self.export()
    }

    pub fn ptr(&self) -> *const c_void {
        match self.export() {
            Some(e) => e.get(),
            None => core::ptr::null(),
        }
    }

    /// Call as `extern "C" fn()`; returns -1 if not hooked / null ptr.
    pub unsafe fn call0(&self) -> i32 {
        let p = self.ptr();
        if p.is_null() {
            return -1;
        }
        let f: extern "C" fn() = core::mem::transmute(p);
        f();
        0
    }
}

/// Fixed-capacity registry state: `N` exports + `I` imports.
///
/// Prefer the `reg_mod!` macro for named field access; indexes are an
/// implementation detail of this storage.
pub struct RegModStatic<const N: usize, const I: usize> {
    pub exports: [RegExport; N],
    pub imports: [RegImport; I],
}

impl<const N: usize, const I: usize> RegModStatic<N, I> {
    pub const fn new(exports: [RegExport; N], imports: [RegImport; I]) -> Self {
        Self { exports, imports }
    }
}
