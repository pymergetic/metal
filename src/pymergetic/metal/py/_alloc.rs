//! Upy/Metal alloc — one heap: `pm_metal_mem_*` (Locked #3).

use pymergetic_metal_mem::{pm_metal_mem_alloc, pm_metal_mem_free, pm_metal_mem_realloc};

pub unsafe fn alloc(size: usize) -> *mut u8 {
    pm_metal_mem_alloc(size)
}

pub unsafe fn free(ptr: *mut u8) {
    pm_metal_mem_free(ptr)
}

pub unsafe fn realloc(ptr: *mut u8, size: usize) -> *mut u8 {
    pm_metal_mem_realloc(ptr, size)
}
