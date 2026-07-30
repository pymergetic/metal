//! FFI + Metal C border over vendored Conte TLSF 3.1 (`external/tlsf`).
//! No local reimplementation — same library product EFI/BIOS already uses.
#![allow(dead_code)]

use core::ffi::c_void;

/// Opaque Conte control block (`tlsf_t`).
pub type Tlsf = c_void;
/// Opaque pool (`pool_t`).
pub type Pool = c_void;

extern "C" {
    pub fn tlsf_create(mem: *mut c_void) -> *mut Tlsf;
    pub fn tlsf_create_with_pool(mem: *mut c_void, bytes: usize) -> *mut Tlsf;
    pub fn tlsf_destroy(tlsf: *mut Tlsf);
    pub fn tlsf_get_pool(tlsf: *mut Tlsf) -> *mut Pool;
    pub fn tlsf_add_pool(tlsf: *mut Tlsf, mem: *mut c_void, bytes: usize) -> *mut Pool;
    pub fn tlsf_remove_pool(tlsf: *mut Tlsf, pool: *mut Pool);
    pub fn tlsf_malloc(tlsf: *mut Tlsf, bytes: usize) -> *mut c_void;
    pub fn tlsf_memalign(tlsf: *mut Tlsf, align: usize, bytes: usize) -> *mut c_void;
    pub fn tlsf_realloc(tlsf: *mut Tlsf, ptr: *mut c_void, size: usize) -> *mut c_void;
    pub fn tlsf_free(tlsf: *mut Tlsf, ptr: *mut c_void);
    pub fn tlsf_block_size(ptr: *mut c_void) -> usize;
    pub fn tlsf_size() -> usize;
    pub fn tlsf_align_size() -> usize;
    pub fn tlsf_block_size_min() -> usize;
    pub fn tlsf_block_size_max() -> usize;
    pub fn tlsf_pool_overhead() -> usize;
    pub fn tlsf_alloc_overhead() -> usize;
    pub fn tlsf_walk_pool(
        pool: *mut Pool,
        walker: Option<unsafe extern "C" fn(*mut c_void, usize, i32, *mut c_void)>,
        user: *mut c_void,
    );
    pub fn tlsf_check(tlsf: *mut Tlsf) -> i32;
    pub fn tlsf_check_pool(pool: *mut Pool) -> i32;
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_tlsf_size() -> usize {
    unsafe { tlsf_size() }
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_tlsf_pool_overhead() -> usize {
    unsafe { tlsf_pool_overhead() }
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_tlsf_align_size() -> usize {
    unsafe { tlsf_align_size() }
}

#[no_mangle]
pub extern "C" fn pm_metal_mem_tlsf_alloc_overhead() -> usize {
    unsafe { tlsf_alloc_overhead() }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_create_with_pool(
    mem: *mut u8,
    bytes: usize,
) -> *mut Tlsf {
    tlsf_create_with_pool(mem as *mut c_void, bytes)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_get_pool(t: *mut Tlsf) -> *mut Pool {
    if t.is_null() {
        return core::ptr::null_mut();
    }
    tlsf_get_pool(t)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_add_pool(
    t: *mut Tlsf,
    mem: *mut u8,
    bytes: usize,
) -> *mut Pool {
    if t.is_null() {
        return core::ptr::null_mut();
    }
    tlsf_add_pool(t, mem as *mut c_void, bytes)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_malloc(t: *mut Tlsf, size: usize) -> *mut u8 {
    if t.is_null() {
        return core::ptr::null_mut();
    }
    tlsf_malloc(t, size) as *mut u8
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_memalign(
    t: *mut Tlsf,
    align: usize,
    size: usize,
) -> *mut u8 {
    if t.is_null() {
        return core::ptr::null_mut();
    }
    tlsf_memalign(t, align, size) as *mut u8
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_realloc(
    t: *mut Tlsf,
    ptr: *mut u8,
    size: usize,
) -> *mut u8 {
    if t.is_null() {
        return core::ptr::null_mut();
    }
    tlsf_realloc(t, ptr as *mut c_void, size) as *mut u8
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_free(t: *mut Tlsf, p: *mut u8) {
    if t.is_null() {
        return;
    }
    tlsf_free(t, p as *mut c_void)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_block_size(ptr: *mut u8) -> usize {
    if ptr.is_null() {
        return 0;
    }
    tlsf_block_size(ptr as *mut c_void)
}
