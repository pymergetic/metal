/* memcpy element-vs-byte: pointer-typed copy_nonoverlapping must scale
 * the count by sizeof(*src). Caught 2026-08-28 by AST corruption that
 * traced back to Node_set_kids copying count BYTES instead of count
 * pointer ELEMENTS. */
#[repr(C)]
pub struct Kid {
    p: *mut u8,
    n: usize,
}

pub fn pack(src: *mut *mut Kid, dst: *mut *mut Kid, count: usize) -> i32 {
    unsafe {
        core::ptr::copy_nonoverlapping(src, dst, count);
    }
    count as i32
}

/* u8 copies must NOT scale (sizeof(*u8) == 1). */
pub fn copy_bytes(src: *const u8, dst: *mut u8, n: usize) -> u8 {
    unsafe {
        core::ptr::copy_nonoverlapping(src, dst, n);
    }
    unsafe { *dst }
}