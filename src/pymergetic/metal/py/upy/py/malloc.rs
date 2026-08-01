//! malloc — upy `m_*` allocators → Metal heap (no GC).

use core::ptr;

use pymergetic_metal_mem::{pm_metal_mem_alloc, pm_metal_mem_free, pm_metal_mem_realloc};

/// Allocate `n` bytes from Metal. Returns null only if `n == 0` fails or OOM.
pub unsafe fn m_malloc(n: usize) -> *mut u8 {
    if n == 0 {
        return ptr::null_mut();
    }
    pm_metal_mem_alloc(n)
}

/// Allocate and zero-fill.
pub unsafe fn m_malloc0(n: usize) -> *mut u8 {
    let p = m_malloc(n);
    if !p.is_null() {
        p.write_bytes(0, n);
    }
    p
}

pub unsafe fn m_free(p: *mut u8) {
    if !p.is_null() {
        pm_metal_mem_free(p);
    }
}

pub unsafe fn m_realloc(p: *mut u8, new_n: usize) -> *mut u8 {
    if new_n == 0 {
        m_free(p);
        return ptr::null_mut();
    }
    if p.is_null() {
        return m_malloc(new_n);
    }
    pm_metal_mem_realloc(p, new_n)
}
