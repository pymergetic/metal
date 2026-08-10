//! Heap linked-chunk buffer for cold diagnostic dumps (ledger / completeness).
//!
//! Chunks are allocated with `pm_metal_mem_alloc` only when bytes are written —
//! no BSS slab, no oversized stack locals in µPy faces.

use core::ffi::c_void;
use core::ptr;

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_mem_alloc(bytes: usize) -> *mut c_void;
    fn pm_metal_mem_free(ptr: *mut c_void);
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_mem_alloc(bytes: usize) -> *mut c_void {
    extern "C" {
        fn malloc(size: usize) -> *mut c_void;
    }
    if bytes == 0 {
        return ptr::null_mut();
    }
    malloc(bytes)
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_mem_free(p: *mut c_void) {
    extern "C" {
        fn free(ptr: *mut c_void);
    }
    if !p.is_null() {
        free(p);
    }
}

const CHUNK_DATA: usize = 4096;

#[repr(C)]
struct Chunk {
    next: *mut Chunk,
    len: usize,
    data: [u8; CHUNK_DATA],
}

/// Growable linked byte buffer on the Metal heap.
pub struct DynBuf {
    head: *mut Chunk,
    tail: *mut Chunk,
    total: usize,
}

impl DynBuf {
    pub const fn new() -> Self {
        Self {
            head: ptr::null_mut(),
            tail: ptr::null_mut(),
            total: 0,
        }
    }

    pub fn len(&self) -> usize {
        self.total
    }

    pub fn clear(&mut self) {
        self.free_chunks();
        self.head = ptr::null_mut();
        self.tail = ptr::null_mut();
        self.total = 0;
    }

    fn free_chunks(&mut self) {
        let mut c = self.head;
        while !c.is_null() {
            unsafe {
                let next = (*c).next;
                pm_metal_mem_free(c.cast());
                c = next;
            }
        }
    }

    fn grow(&mut self) -> Option<*mut Chunk> {
        let p = unsafe { pm_metal_mem_alloc(core::mem::size_of::<Chunk>()) } as *mut Chunk;
        if p.is_null() {
            return None;
        }
        unsafe {
            (*p).next = ptr::null_mut();
            (*p).len = 0;
        }
        if self.head.is_null() {
            self.head = p;
            self.tail = p;
        } else {
            unsafe {
                (*self.tail).next = p;
            }
            self.tail = p;
        }
        Some(p)
    }

    pub fn push(&mut self, b: u8) -> Option<()> {
        let need_new = self.tail.is_null() || unsafe { (*self.tail).len >= CHUNK_DATA };
        if need_new {
            self.grow()?;
        }
        unsafe {
            let t = self.tail;
            let i = (*t).len;
            (*t).data[i] = b;
            (*t).len = i + 1;
        }
        self.total += 1;
        Some(())
    }

    pub fn append(&mut self, bytes: &[u8]) -> Option<()> {
        for &b in bytes {
            self.push(b)?;
        }
        Some(())
    }

    /// Copy into `dst`; returns false if `dst` is too small.
    pub fn copy_to(&self, dst: &mut [u8]) -> bool {
        if self.total > dst.len() {
            return false;
        }
        let mut off = 0usize;
        let mut c = self.head;
        while !c.is_null() {
            unsafe {
                let n = (*c).len;
                let src = core::ptr::addr_of!((*c).data) as *const u8;
                ptr::copy_nonoverlapping(src, dst.as_mut_ptr().add(off), n);
                off += n;
                c = (*c).next;
            }
        }
        true
    }

    /// Flatten into one `pm_metal_mem_alloc` block; frees chunks. Caller frees
    /// the returned pointer with `pm_metal_mem_free`. Empty → null + len 0.
    pub fn take_flat(mut self) -> (*mut u8, usize) {
        let n = self.total;
        if n == 0 {
            self.clear();
            core::mem::forget(self);
            return (ptr::null_mut(), 0);
        }
        let flat = unsafe { pm_metal_mem_alloc(n) } as *mut u8;
        if flat.is_null() {
            self.clear();
            core::mem::forget(self);
            return (ptr::null_mut(), 0);
        }
        let mut off = 0usize;
        let mut c = self.head;
        while !c.is_null() {
            unsafe {
                let cn = (*c).len;
                ptr::copy_nonoverlapping((*c).data.as_ptr(), flat.add(off), cn);
                off += cn;
                let next = (*c).next;
                pm_metal_mem_free(c.cast());
                c = next;
            }
        }
        self.head = ptr::null_mut();
        self.tail = ptr::null_mut();
        self.total = 0;
        core::mem::forget(self);
        (flat, n)
    }
}

impl Drop for DynBuf {
    fn drop(&mut self) {
        self.clear();
    }
}
