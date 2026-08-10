//! Helpers for floor modules that publish a fixed export set into the
//! static lifecycle so always-proxy faces can resolve them.

use core::ffi::c_void;

use crate::entry::RegExport;
use crate::ledger::{self, HONESTY_OK, LANG_C, ROLE_MUSCLE};

/// Publish each `(export, fn_ptr)` pair. Returns 0.
/// When `module` is non-empty, also appends cold-ledger callee rows (inspect).
pub fn publish_exports(exports: &[RegExport], ptrs: &[*const c_void]) -> i32 {
    publish_exports_meta(b"", exports, ptrs)
}

/// Like [`publish_exports`], and records cold-ledger callees under `module`.
pub fn publish_exports_meta(
    module: &[u8],
    exports: &[RegExport],
    ptrs: &[*const c_void],
) -> i32 {
    let n = exports.len().min(ptrs.len());
    for i in 0..n {
        exports[i].publish(ptrs[i]);
        if !module.is_empty() {
            let _ = ledger::LEDGER.add_callee(
                module,
                exports[i].name.as_bytes(),
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
