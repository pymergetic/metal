//! Helpers for floor modules that publish a fixed export set into the
//! static lifecycle so always-proxy faces can resolve them.

use core::ffi::c_void;

use crate::entry::RegEntry;

/// Publish each `(entry, fn_ptr)` pair. Returns 0.
pub fn publish_entries(entries: &[RegEntry], ptrs: &[*const c_void]) -> i32 {
    let n = entries.len().min(ptrs.len());
    for i in 0..n {
        entries[i].publish(ptrs[i]);
    }
    0
}
