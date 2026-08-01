//! Metal host heap — nested `arena/` + `tlsf/` + `lock/`.
//! Package entry ``__init__.rs``.
//!
//! Locking (product `mem.c` / `arena.c`):
//! - arena embedded spin: map / unmap / heap_grow / size queries
//! - [`HEAP`].heap_lock: TLSF malloc / free / pool grow / free_bytes walk
//! Lock order when both needed: heap_lock then arena.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use pymergetic_metal_rt as _;

#[path = "../arena/__init__.rs"]
mod arena;
#[path = "../lock/__init__.rs"]
pub mod lock;
#[path = "../tlsf/__init__.rs"]
mod tlsf;

use core::ffi::c_void;
use core::ptr::{addr_of, addr_of_mut};

use arena::Arena;
use lock::Spin;
use tlsf::{
    tlsf_add_pool, tlsf_block_size, tlsf_create_with_pool, tlsf_free, tlsf_get_pool, tlsf_malloc,
    tlsf_memalign, tlsf_pool_overhead, tlsf_realloc, tlsf_size, tlsf_walk_pool, Pool, Tlsf,
};

/// User-facing alloc alignment (Metal host heap).
const USER_ALIGN: usize = 16;
const MAX_POOLS: usize = 32;
/// Prefer this much for the first TLSF pool (capped by hole/2).
const SEED_TARGET: usize = 128 * 1024 * 1024;
const SEED_FLOOR: usize = 256 * 1024;
/// Grow heap by this when a pool is exhausted (capped by hole).
const GROW_DEFAULT: usize = 16 * 1024 * 1024;

struct Heap {
    arena: Arena,
    heap_lock: Spin,
    tlsf: *mut Tlsf,
    pools: [*mut Pool; MAX_POOLS],
    pool_count: usize,
}

static mut HEAP: Heap = Heap {
    arena: Arena::empty(),
    heap_lock: Spin::new(),
    tlsf: core::ptr::null_mut(),
    pools: [core::ptr::null_mut(); MAX_POOLS],
    pool_count: 0,
};

fn align_up(x: usize, a: usize) -> usize {
    arena::align_up(x, a)
}

unsafe fn seed_bytes(hole: usize) -> usize {
    let ctl = tlsf_size() + tlsf_pool_overhead();
    let floor = ctl + SEED_FLOOR;
    let mut seed = SEED_TARGET;
    if seed > hole / 2 {
        seed = hole / 2;
    }
    if seed < floor {
        seed = floor;
    }
    if seed > hole {
        seed = hole;
    }
    arena::align_down(seed, arena::PAGE_SIZE)
}

unsafe fn note_pool(pool: *mut Pool) {
    let heap = &mut *addr_of_mut!(HEAP);
    if pool.is_null() || heap.pool_count >= MAX_POOLS {
        return;
    }
    heap.pools[heap.pool_count] = pool;
    heap.pool_count += 1;
}

/// Caller must hold [`Heap::heap_lock`].
unsafe fn grow_and_add_pool(need: usize) -> bool {
    let heap = &mut *addr_of_mut!(HEAP);
    if heap.tlsf.is_null() || heap.pool_count >= MAX_POOLS {
        return false;
    }
    let mut grow = GROW_DEFAULT;
    let want = need + need / 8 + 64 * 1024;
    if want > grow {
        grow = want;
    }
    grow = align_up(grow, arena::PAGE_SIZE);
    let hole = heap.arena.hole();
    if grow > hole {
        grow = arena::align_down(hole, arena::PAGE_SIZE);
    }
    if grow < tlsf_pool_overhead() + 4096 {
        return false;
    }
    let p = heap.arena.heap_grow(grow);
    if p.is_null() {
        return false;
    }
    let pool = tlsf_add_pool(heap.tlsf, p as *mut c_void, grow);
    if pool.is_null() {
        return false;
    }
    note_pool(pool);
    true
}

unsafe extern "C" fn free_bytes_walk(
    _ptr: *mut c_void,
    size: usize,
    used: i32,
    user: *mut c_void,
) {
    if used == 0 {
        let sum = user as *mut usize;
        *sum = (*sum).saturating_add(size);
    }
}

unsafe fn tlsf_free_bytes(heap: &Heap) -> usize {
    let mut sum: usize = 0;
    for i in 0..heap.pool_count {
        let pool = heap.pools[i];
        if !pool.is_null() {
            tlsf_walk_pool(pool, Some(free_bytes_walk), &mut sum as *mut usize as *mut c_void);
        }
    }
    sum
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_init(base: *mut u8, bytes: usize) -> i32 {
    let heap = &mut *addr_of_mut!(HEAP);
    *heap = Heap {
        arena: Arena::empty(),
        heap_lock: Spin::new(),
        tlsf: core::ptr::null_mut(),
        pools: [core::ptr::null_mut(); MAX_POOLS],
        pool_count: 0,
    };
    if heap.arena.init(base, bytes) != 0 {
        return -1;
    }
    let seed = seed_bytes(heap.arena.hole());
    if seed == 0 {
        return -1;
    }
    let region = heap.arena.heap_grow(seed);
    if region.is_null() {
        return -1;
    }
    let ctl = tlsf_create_with_pool(region as *mut c_void, seed);
    if ctl.is_null() {
        return -1;
    }
    heap.tlsf = ctl;
    note_pool(tlsf_get_pool(ctl));
    0
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_alloc(size: usize) -> *mut u8 {
    let heap = &*addr_of!(HEAP);
    if size == 0 || heap.tlsf.is_null() {
        return core::ptr::null_mut();
    }
    let need = align_up(size, USER_ALIGN);
    heap.heap_lock.lock();
    let mut p = tlsf_malloc(heap.tlsf, need) as *mut u8;
    if p.is_null() && grow_and_add_pool(need) {
        let heap = &*addr_of!(HEAP);
        p = tlsf_malloc(heap.tlsf, need) as *mut u8;
    }
    heap.heap_lock.unlock();
    p
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_free(ptr: *mut u8) {
    let heap = &*addr_of!(HEAP);
    if ptr.is_null() || heap.tlsf.is_null() {
        return;
    }
    heap.heap_lock.lock();
    tlsf_free(heap.tlsf, ptr as *mut c_void);
    heap.heap_lock.unlock();
}

/// Aligned heap alloc (TLSF `tlsf_memalign`). `align` must be a power of two.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_memalign(align: usize, size: usize) -> *mut u8 {
    let heap = &*addr_of!(HEAP);
    if size == 0 || align == 0 || !align.is_power_of_two() || heap.tlsf.is_null() {
        return core::ptr::null_mut();
    }
    heap.heap_lock.lock();
    let mut p = tlsf_memalign(heap.tlsf, align, size) as *mut u8;
    if p.is_null() && grow_and_add_pool(size.saturating_add(align)) {
        let heap = &*addr_of!(HEAP);
        p = tlsf_memalign(heap.tlsf, align, size) as *mut u8;
    }
    heap.heap_lock.unlock();
    p
}

/// Resize heap block (TLSF). `ptr == null` → alloc; `size == 0` → free.
/// On in-pool failure: alloc + copy MIN(old, new) + free old (METAL-001).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_realloc(ptr: *mut u8, size: usize) -> *mut u8 {
    if size == 0 {
        pm_metal_mem_free(ptr);
        return core::ptr::null_mut();
    }
    if ptr.is_null() {
        return pm_metal_mem_alloc(size);
    }
    let heap = &*addr_of!(HEAP);
    if heap.tlsf.is_null() {
        return core::ptr::null_mut();
    }
    let need = align_up(size, USER_ALIGN);
    heap.heap_lock.lock();
    let n = tlsf_realloc(heap.tlsf, ptr as *mut c_void, need) as *mut u8;
    let old_size = if n.is_null() {
        tlsf_block_size(ptr as *mut c_void)
    } else {
        0
    };
    heap.heap_lock.unlock();
    if !n.is_null() {
        return n;
    }
    let n = pm_metal_mem_alloc(size);
    if !n.is_null() {
        let copy = if old_size < size { old_size } else { size };
        core::ptr::copy_nonoverlapping(ptr, n, copy);
        pm_metal_mem_free(ptr);
    }
    n
}

/// Page-aligned map span (grows up). LIFO unmap only. Not TLSF.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_map(bytes: usize) -> *mut u8 {
    let heap = &mut *addr_of_mut!(HEAP);
    if !heap.arena.ready() || bytes == 0 {
        return core::ptr::null_mut();
    }
    heap.arena.map(bytes)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_unmap(ptr: *mut u8, bytes: usize) -> i32 {
    let heap = &mut *addr_of_mut!(HEAP);
    if !heap.arena.ready() {
        return -1;
    }
    heap.arena.unmap(ptr, bytes)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_heap_bytes() -> usize {
    (*addr_of!(HEAP)).arena.bytes()
}

/// Bytes claimed by the upward map span (LIFO pages).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_map_used() -> usize {
    (*addr_of!(HEAP)).arena.map_used()
}

/// Bytes claimed by the downward TLSF pool span.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_tlsf_used() -> usize {
    (*addr_of!(HEAP)).arena.heap_used()
}

/// Unclaimed gap between map brk and TLSF brk.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_hole() -> usize {
    (*addr_of!(HEAP)).arena.hole()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_mem_free_bytes() -> usize {
    let heap = &*addr_of!(HEAP);
    if heap.tlsf.is_null() {
        return 0;
    }
    heap.heap_lock.lock();
    let tlsf_free = tlsf_free_bytes(heap);
    heap.heap_lock.unlock();
    tlsf_free.saturating_add(heap.arena.hole())
}

/// Rust-side helpers.
pub mod api {
    pub fn init(base: *mut u8, bytes: usize) -> i32 {
        unsafe { super::pm_metal_mem_init(base, bytes) }
    }

    pub fn alloc(size: usize) -> *mut u8 {
        unsafe { super::pm_metal_mem_alloc(size) }
    }

    pub fn free(ptr: *mut u8) {
        unsafe { super::pm_metal_mem_free(ptr) }
    }

    pub fn realloc(ptr: *mut u8, size: usize) -> *mut u8 {
        unsafe { super::pm_metal_mem_realloc(ptr, size) }
    }

    pub fn memalign(align: usize, size: usize) -> *mut u8 {
        unsafe { super::pm_metal_mem_memalign(align, size) }
    }

    pub fn map(bytes: usize) -> *mut u8 {
        unsafe { super::pm_metal_mem_map(bytes) }
    }

    pub fn unmap(ptr: *mut u8, bytes: usize) -> i32 {
        unsafe { super::pm_metal_mem_unmap(ptr, bytes) }
    }
}
