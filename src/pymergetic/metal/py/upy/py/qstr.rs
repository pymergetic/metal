//! qstr pool — static table + dynamic intern (Metal alloc, no GC).

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

use pymergetic_metal_mem::{pm_metal_mem_alloc, pm_metal_mem_free};

use super::mpconfig;
use super::qstrdefs::{self, Qstr, STATIC_STRS};

pub type QstrHash = u16;
pub type QstrLen = u8;

const DYN_MAX: usize = 256;
const STR_CAP: usize = 128;

struct Spin {
    state: AtomicU32,
}

impl Spin {
    const fn new() -> Self {
        Self {
            state: AtomicU32::new(0),
        }
    }
    fn lock(&self) {
        while self
            .state
            .compare_exchange_weak(0, 1, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
    }
    fn unlock(&self) {
        self.state.store(0, Ordering::Release);
    }
}

#[derive(Clone, Copy)]
struct DynEntry {
    used: bool,
    hash: QstrHash,
    len: u8,
    ptr: *mut u8,
}

impl DynEntry {
    const fn empty() -> Self {
        Self {
            used: false,
            hash: 0,
            len: 0,
            ptr: core::ptr::null_mut(),
        }
    }
}

struct Pool {
    lock: Spin,
    ready: AtomicBool,
    dyn_entries: UnsafeCell<[DynEntry; DYN_MAX]>,
    dyn_len: UnsafeCell<usize>,
}

// Safety: mutations under `lock`.
unsafe impl Sync for Pool {}

impl Pool {
    const fn new() -> Self {
        Self {
            lock: Spin::new(),
            ready: AtomicBool::new(false),
            dyn_entries: UnsafeCell::new([DynEntry::empty(); DYN_MAX]),
            dyn_len: UnsafeCell::new(0),
        }
    }
}

static POOL: Pool = Pool::new();

/// djb2 truncated; never zero (upstream `qstr_compute_hash`).
pub fn compute_hash(data: &[u8]) -> QstrHash {
    let mut hash: u32 = 5381;
    for &b in data {
        hash = hash.wrapping_mul(33) ^ (b as u32);
    }
    let mask = (1u32 << (8 * mpconfig::QSTR_BYTES_IN_HASH as u32)) - 1;
    let mut h = (hash & mask) as QstrHash;
    if h == 0 {
        h = 1;
    }
    h
}

pub fn init() {
    POOL.ready.store(true, Ordering::Release);
}

fn ensure_ready() {
    if !POOL.ready.load(Ordering::Acquire) {
        init();
    }
}

fn dyn_eq(data: &[u8], ptr: *const u8, len: usize) -> bool {
    if data.len() != len {
        return false;
    }
    if len == 0 {
        return true;
    }
    unsafe { core::slice::from_raw_parts(ptr, len) == data }
}

fn find_static(data: &[u8]) -> Option<Qstr> {
    for (i, s) in STATIC_STRS.iter().enumerate() {
        if *s == data {
            return Some(i);
        }
    }
    None
}

/// Find existing qstr; `None` if missing. Empty string is static id 0.
pub fn find_strn(data: &[u8]) -> Option<Qstr> {
    ensure_ready();
    if data.len() > STR_CAP {
        return None;
    }
    if let Some(q) = find_static(data) {
        return Some(q);
    }
    let h = compute_hash(data);
    POOL.lock.lock();
    let entries = unsafe { &*POOL.dyn_entries.get() };
    let n = unsafe { *POOL.dyn_len.get() };
    let mut found = None;
    for i in 0..n {
        let e = entries[i];
        if e.used && e.hash == h && dyn_eq(data, e.ptr, e.len as usize) {
            found = Some(STATIC_STRS.len() + i);
            break;
        }
    }
    POOL.lock.unlock();
    found
}

/// Intern `data`; allocates a Metal copy for new dynamic entries.
pub fn from_strn(data: &[u8]) -> Qstr {
    ensure_ready();
    if data.len() > STR_CAP {
        return qstrdefs::QSTR_NULL;
    }
    if let Some(q) = find_strn(data) {
        return q;
    }
    let h = compute_hash(data);
    POOL.lock.lock();
    let rc = unsafe { intern_locked(data, h) };
    POOL.lock.unlock();
    rc
}

pub fn from_str(s: &str) -> Qstr {
    from_strn(s.as_bytes())
}

unsafe fn intern_locked(data: &[u8], h: QstrHash) -> Qstr {
    let entries = &mut *POOL.dyn_entries.get();
    let n = *POOL.dyn_len.get();
    for i in 0..n {
        let e = entries[i];
        if e.used && e.hash == h && dyn_eq(data, e.ptr, e.len as usize) {
            return STATIC_STRS.len() + i;
        }
    }
    if n >= DYN_MAX {
        return qstrdefs::QSTR_NULL;
    }
    let buf = pm_metal_mem_alloc(data.len() + 1);
    if buf.is_null() {
        return qstrdefs::QSTR_NULL;
    }
    core::ptr::copy_nonoverlapping(data.as_ptr(), buf, data.len());
    *buf.add(data.len()) = 0;
    entries[n] = DynEntry {
        used: true,
        hash: h,
        len: data.len() as u8,
        ptr: buf,
    };
    *POOL.dyn_len.get() = n + 1;
    STATIC_STRS.len() + n
}

/// Bytes for `q`. Dynamic entries remain valid until `reset_dynamic_for_test`.
pub fn str(q: Qstr) -> &'static [u8] {
    ensure_ready();
    if q < STATIC_STRS.len() {
        return STATIC_STRS[q];
    }
    let i = q - STATIC_STRS.len();
    POOL.lock.lock();
    let entries = unsafe { &*POOL.dyn_entries.get() };
    let n = unsafe { *POOL.dyn_len.get() };
    let out: &'static [u8] = if i < n && entries[i].used {
        let e = entries[i];
        unsafe { core::slice::from_raw_parts(e.ptr, e.len as usize) }
    } else {
        b""
    };
    POOL.lock.unlock();
    out
}

pub fn len(q: Qstr) -> usize {
    str(q).len()
}

pub fn hash(q: Qstr) -> QstrHash {
    compute_hash(str(q))
}

/// Host/test helper: drop dynamic entries (does not free static).
pub unsafe fn reset_dynamic_for_test() {
    POOL.lock.lock();
    let entries = &mut *POOL.dyn_entries.get();
    let n = *POOL.dyn_len.get();
    for i in 0..n {
        if entries[i].used && !entries[i].ptr.is_null() {
            pm_metal_mem_free(entries[i].ptr);
            entries[i] = DynEntry::empty();
        }
    }
    *POOL.dyn_len.get() = 0;
    POOL.lock.unlock();
}
