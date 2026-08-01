//! gc — DEAD (Locked #5). No collector; Metal owns the heap.
//!
//! Call sites that still name `gc_*` get honest no-ops / Metal redirect.
//! Do not grow this into a real GC.

use super::malloc;

/// Ignored — there is no GC heap range.
pub fn init(_start: *mut u8, _end: *mut u8) {}

pub fn lock() {}
pub fn unlock() {}
pub fn is_locked() -> bool {
    false
}

pub fn collect() {}
pub fn collect_start() {}
pub fn collect_end() {}
pub fn collect_root(_ptrs: *mut *mut u8, _len: usize) {}
pub fn sweep_all() {}

/// Redirect to Metal malloc (flags ignored — no finalisers).
pub unsafe fn alloc(n_bytes: usize, _flags: u32) -> *mut u8 {
    malloc::m_malloc(n_bytes)
}

pub unsafe fn free(ptr: *mut u8) {
    malloc::m_free(ptr)
}

pub unsafe fn realloc(ptr: *mut u8, n_bytes: usize, _allow_move: bool) -> *mut u8 {
    malloc::m_realloc(ptr, n_bytes)
}

#[derive(Clone, Copy, Default)]
pub struct Info {
    pub total: usize,
    pub used: usize,
    pub free: usize,
}

/// Always zeros — no GC arena to measure.
pub fn info() -> Info {
    Info::default()
}
