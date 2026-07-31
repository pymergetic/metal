//! Dual-span arena: map grows up, heap (TLSF pools) grows down.
//! Nested Metal module; C ABI `pm_metal_mem_arena_*` for codegen.
//!
//! Mutating ops and size queries take the embedded spin (product `arena.c`
//! `mLock`). Layout: four `size_t` spans + `uint32_t` lock word (same as
//! [`crate::lock::Spin`]).
#![allow(dead_code)]

use crate::lock::Spin;

pub const PAGE_SIZE: usize = 4096;
const MIN_BYTES: usize = PAGE_SIZE * 8;

#[repr(C)]
pub struct Arena {
    pub base: usize,
    pub end: usize,
    pub map_brk: usize,
    pub heap_brk: usize,
    /// Spin state word — layout-compatible with [`Spin`].
    pub lock: u32,
}

impl Arena {
    pub const fn empty() -> Self {
        Self {
            base: 0,
            end: 0,
            map_brk: 0,
            heap_brk: 0,
            lock: 0,
        }
    }

    fn spin(&self) -> &Spin {
        // Safety: Spin is #[repr(C)] { AtomicU32 }; same size/align as u32 word.
        unsafe { &*(&self.lock as *const u32 as *const Spin) }
    }

    pub fn ready(&self) -> bool {
        self.base != 0
    }

    pub unsafe fn init(&mut self, base: *mut u8, bytes: usize) -> i32 {
        if base.is_null() || bytes < MIN_BYTES {
            return -1;
        }
        let b = base as usize;
        if b % PAGE_SIZE != 0 {
            return -1;
        }
        if b.checked_add(bytes).is_none() {
            return -1;
        }
        let bytes_al = align_down(bytes, PAGE_SIZE);
        if bytes_al < MIN_BYTES {
            return -1;
        }
        let end = b + bytes_al;
        self.base = b;
        self.end = end;
        self.map_brk = b;
        self.heap_brk = end;
        self.spin().init();
        0
    }

    pub fn bytes(&self) -> usize {
        if !self.ready() {
            return 0;
        }
        self.spin().with_lock(|| self.end.saturating_sub(self.base))
    }

    pub fn map_used(&self) -> usize {
        if !self.ready() {
            return 0;
        }
        self.spin()
            .with_lock(|| self.map_brk.saturating_sub(self.base))
    }

    pub fn heap_used(&self) -> usize {
        if !self.ready() {
            return 0;
        }
        self.spin()
            .with_lock(|| self.end.saturating_sub(self.heap_brk))
    }

    pub fn hole(&self) -> usize {
        if !self.ready() {
            return 0;
        }
        self.spin()
            .with_lock(|| self.heap_brk.saturating_sub(self.map_brk))
    }

    /// Grow heap downward; returns start of new region, or null.
    pub unsafe fn heap_grow(&mut self, bytes: usize) -> *mut u8 {
        if !self.ready() || bytes == 0 {
            return core::ptr::null_mut();
        }
        let need = align_up(bytes, PAGE_SIZE);
        self.spin().lock();
        if need > self.heap_brk.saturating_sub(self.map_brk) {
            self.spin().unlock();
            return core::ptr::null_mut();
        }
        self.heap_brk -= need;
        let p = self.heap_brk as *mut u8;
        self.spin().unlock();
        p
    }

    /// Grow map upward; returns start of new region, or null.
    pub unsafe fn map(&mut self, bytes: usize) -> *mut u8 {
        if !self.ready() || bytes == 0 {
            return core::ptr::null_mut();
        }
        let need = align_up(bytes, PAGE_SIZE);
        self.spin().lock();
        if need > self.heap_brk.saturating_sub(self.map_brk) {
            self.spin().unlock();
            return core::ptr::null_mut();
        }
        let p = self.map_brk as *mut u8;
        self.map_brk += need;
        self.spin().unlock();
        p
    }

    /// LIFO unmap at map frontier.
    pub unsafe fn unmap(&mut self, ptr: *mut u8, bytes: usize) -> i32 {
        if !self.ready() || ptr.is_null() || bytes == 0 {
            return -1;
        }
        let need = align_up(bytes, PAGE_SIZE);
        let start = ptr as usize;
        self.spin().lock();
        let ok = self.map_brk >= need && start == self.map_brk - need;
        if !ok {
            self.spin().unlock();
            return -1;
        }
        self.map_brk = start;
        self.spin().unlock();
        0
    }
}

pub fn align_up(x: usize, a: usize) -> usize {
    (x + (a - 1)) & !(a - 1)
}

pub fn align_down(x: usize, a: usize) -> usize {
    x & !(a - 1)
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_arena_empty() -> Arena {
    Arena::empty()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_init(a: *mut Arena, base: *mut u8, bytes: usize) -> i32 {
    if a.is_null() {
        return -1;
    }
    (*a).init(base, bytes)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_ready(a: *const Arena) -> i32 {
    if a.is_null() {
        return 0;
    }
    if (*a).ready() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_bytes(a: *const Arena) -> usize {
    if a.is_null() {
        return 0;
    }
    (*a).bytes()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_map_used(a: *const Arena) -> usize {
    if a.is_null() {
        return 0;
    }
    (*a).map_used()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_heap_used(a: *const Arena) -> usize {
    if a.is_null() {
        return 0;
    }
    (*a).heap_used()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_hole(a: *const Arena) -> usize {
    if a.is_null() {
        return 0;
    }
    (*a).hole()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_heap_grow(a: *mut Arena, bytes: usize) -> *mut u8 {
    if a.is_null() {
        return core::ptr::null_mut();
    }
    (*a).heap_grow(bytes)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_map(a: *mut Arena, bytes: usize) -> *mut u8 {
    if a.is_null() {
        return core::ptr::null_mut();
    }
    (*a).map(bytes)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_arena_unmap(a: *mut Arena, ptr: *mut u8, bytes: usize) -> i32 {
    if a.is_null() {
        return -1;
    }
    (*a).unmap(ptr, bytes)
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_arena_align_up(x: usize, a: usize) -> usize {
    align_up(x, a)
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_arena_align_down(x: usize, a: usize) -> usize {
    align_down(x, a)
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_arena_page_size() -> usize {
    PAGE_SIZE
}
