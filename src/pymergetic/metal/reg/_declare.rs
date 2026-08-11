//! Import slot + fixed-capacity per-module registry state.
//!
//! **Façade:** module *identity* is µPy `sys.modules` (`pm_mod_*`).
//! This slot only caches the resolved call edge after connect — do not grow
//! a parallel module OS here.

use core::ffi::c_void;
use core::sync::atomic::{AtomicPtr, Ordering};

use crate::entry::RegExport;

/// One import: peer `(module, func)` + cached call edge after connect.
///
/// Populated by `_kernel::connect_one` / `connect_all` (or
/// [`crate::resolve_import`]). Resolve order: µPy `pm_mod_resolve_native`
/// first, then RegMod export table (transitional). Reset to null when the
/// peer was never found or has since been unloaded.
///
/// Hot path: [`RegImport::ptr`] — no strings, no `sys.modules` walk.
pub struct RegImport {
    pub module: &'static str,
    pub func: &'static str,
    /// Cached fn / trampoline pointer (SoT connect result).
    fn_slot: AtomicPtr<c_void>,
    /// Optional RegExport meta (ledger / transitional ring).
    slot: AtomicPtr<RegExport>,
}

/// Temporary alias while call sites migrate off the old name.
#[deprecated(note = "renamed to RegImport")]
pub type ImportRow = RegImport;

// Safety: atomics are the only interior mutability.
unsafe impl Sync for RegImport {}

impl RegImport {
    pub const fn new(module: &'static str, func: &'static str) -> Self {
        Self {
            module,
            func,
            fn_slot: AtomicPtr::new(core::ptr::null_mut()),
            slot: AtomicPtr::new(core::ptr::null_mut()),
        }
    }

    pub(crate) fn set(&self, export: Option<&'static RegExport>) {
        let p = match export {
            Some(e) => e as *const RegExport as *mut RegExport,
            None => core::ptr::null_mut(),
        };
        self.slot.store(p, Ordering::Release);
        let fnp = match export {
            Some(e) => e.get() as *mut c_void,
            None => core::ptr::null_mut(),
        };
        if !fnp.is_null() {
            self.fn_slot.store(fnp, Ordering::Release);
        }
    }

    /// Cache a resolved native/trampoline pointer (from `pm_mod_*`).
    pub(crate) fn set_fn(&self, ptr: *mut c_void) {
        self.fn_slot.store(ptr, Ordering::Release);
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
        let p = self.fn_slot.load(Ordering::Acquire);
        if !p.is_null() {
            return p;
        }
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
/// Hand form: module-local `Export` / `Import` enums as named indexes
/// (see `.cursor/rules/metal-regmod-slot-enums.mdc`). Outside the muscle,
/// resolve by string name only. `reg_mod!` remains optional sugar.
pub struct RegModStatic<const N: usize, const I: usize> {
    pub exports: [RegExport; N],
    pub imports: [RegImport; I],
}

impl<const N: usize, const I: usize> RegModStatic<N, I> {
    pub const fn new(exports: [RegExport; N], imports: [RegImport; I]) -> Self {
        Self { exports, imports }
    }
}
