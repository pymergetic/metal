//! Helpers for floor modules that publish a fixed export set into the
//! static lifecycle so always-proxy faces can resolve them.

use core::ffi::c_void;

use crate::entry::RegEntry;
use crate::ledger::{self, HONESTY_OK, LANG_C, ROLE_MUSCLE};

/// Publish each `(entry, fn_ptr)` pair. Returns 0.
/// When `module` is non-empty, also appends cold-ledger callee rows (inspect).
pub fn publish_entries(entries: &[RegEntry], ptrs: &[*const c_void]) -> i32 {
    publish_entries_meta(b"", entries, ptrs)
}

/// Like [`publish_entries`], and records cold-ledger callees under `module`.
pub fn publish_entries_meta(
    module: &[u8],
    entries: &[RegEntry],
    ptrs: &[*const c_void],
) -> i32 {
    let n = entries.len().min(ptrs.len());
    for i in 0..n {
        entries[i].publish(ptrs[i]);
        if !module.is_empty() {
            let _ = ledger::LEDGER.add_callee(
                module,
                entries[i].name.as_bytes(),
                LANG_C,
                ROLE_MUSCLE,
                HONESTY_OK,
                false,
                false,
                b"",
                b"floor_publish",
                ptrs[i],
            );
        }
    }
    0
}
