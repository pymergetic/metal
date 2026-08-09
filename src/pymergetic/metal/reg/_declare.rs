//! Import slot + fixed-capacity per-module registry state.

use core::sync::atomic::{AtomicPtr, Ordering};

use crate::entry::RegEntry;

/// One import slot a module holds for a peer's export -- populated by
/// `_kernel::connect_one`/`connect_all` resolving `(module, func)`
/// against the kernel table, and reset to null whenever the peer was
/// never found or has since been unloaded.
///
/// A generated registry-proxy face reads [`ImportRow::entry`] and calls
/// through the resolved [`RegEntry`]'s pointer directly -- no refcount,
/// no lock: `_kernel::unload` quiesces every async runner before
/// touching any entry, so there is no concurrent caller to race against
/// while a slot is being withdrawn.
pub struct ImportRow {
    pub module: &'static str,
    pub func: &'static str,
    slot: AtomicPtr<RegEntry>,
}

// Safety: `slot` is the only field with interior mutability, and it is an
// atomic.
unsafe impl Sync for ImportRow {}

impl ImportRow {
    pub const fn new(module: &'static str, func: &'static str) -> Self {
        Self {
            module,
            func,
            slot: AtomicPtr::new(core::ptr::null_mut()),
        }
    }

    pub(crate) fn set(&self, entry: Option<&'static RegEntry>) {
        let p = match entry {
            Some(e) => e as *const RegEntry as *mut RegEntry,
            None => core::ptr::null_mut(),
        };
        self.slot.store(p, Ordering::Release);
    }

    pub fn entry(&self) -> Option<&'static RegEntry> {
        let p = self.slot.load(Ordering::Acquire);
        if p.is_null() {
            None
        } else {
            Some(unsafe { &*p })
        }
    }
}

/// Fixed-capacity registry state a module keeps as one static: `N`
/// exported entries (this module's own border) plus `I` import slots
/// (this module's peers). Const-generic so each module picks its own
/// size at zero runtime cost; [`crate::RegMod`] borrows both arrays as
/// plain slices so the kernel table can hold heterogeneous modules
/// without `N`/`I` leaking into its own type.
pub struct RegModStatic<const N: usize, const I: usize> {
    pub entries: [RegEntry; N],
    pub imports: [ImportRow; I],
}

impl<const N: usize, const I: usize> RegModStatic<N, I> {
    pub const fn new(entries: [RegEntry; N], imports: [ImportRow; I]) -> Self {
        Self { entries, imports }
    }
}
