//! frozenmod — frozen module registry (upstream `py/frozenmod.c` mirror,
//! string-only).
//!
//! Upstream carries two frozen kinds: `MICROPY_MODULE_FROZEN_STR` (source
//! text, lexed like any other file) and `MICROPY_MODULE_FROZEN_MPY`
//! (pre-compiled `.mpy` raw code, loaded via `persistentcode.c`). Metal has
//! no `.mpy` loader yet (`persistentcode.rs` is still a `MIRROR` row, not
//! built) -- rather than fake a `Mpy` kind that can only ever answer
//! "unsupported", this mirror carries **only** the frozen-string half:
//! a fixed-capacity, spinlock-protected table of `name -> source bytes`,
//! both `'static` (baked-in firmware source; see [`register_str`]). When
//! the loader exists, a real `Mpy` kind belongs here alongside it, not
//! before.
//!
//! Lookup semantics mirror `mp_find_frozen_module`'s exact-name-match
//! case (`MP_IMPORT_STAT_FILE`); Metal has no VFS directory tree to mirror
//! the `MP_IMPORT_STAT_DIR` prefix case against, so `find`/`load_as_source`
//! only ever answer "this exact name is frozen" or "it isn't" -- an
//! honest `None`, not a silently-wrong directory guess.

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU32, AtomicUsize, Ordering};

/// A registered frozen source module.
#[derive(Clone, Copy)]
pub struct Frozen {
    pub name: &'static str,
    pub src: &'static [u8],
}

const MAX_FROZEN: usize = 32;

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

struct Registry {
    lock: Spin,
    len: AtomicUsize,
    entries: UnsafeCell<[Option<Frozen>; MAX_FROZEN]>,
}

// Safety: `entries` is only touched while `lock` is held.
unsafe impl Sync for Registry {}

impl Registry {
    const fn new() -> Self {
        Self {
            lock: Spin::new(),
            len: AtomicUsize::new(0),
            entries: UnsafeCell::new([None; MAX_FROZEN]),
        }
    }
}

static REGISTRY: Registry = Registry::new();

/// Register a frozen source module (upstream's `MICROPY_MODULE_FROZEN_STR`
/// content, minus the build-time `makecompresseddata.py` packing -- Metal
/// registers each entry individually at init time instead). `name`/`src`
/// are `'static` (e.g. a firmware-baked `include_str!`/literal), so this
/// never allocates.
///
/// Returns `false` (a real, checkable condition, not silent truncation) if
/// `name` is already registered or the fixed-size table is full.
pub fn register_str(name: &'static str, src: &'static [u8]) -> bool {
    REGISTRY.lock.lock();
    let entries = unsafe { &mut *REGISTRY.entries.get() };
    let n = REGISTRY.len.load(Ordering::Relaxed);
    for e in entries.iter().take(n).flatten() {
        if e.name == name {
            REGISTRY.lock.unlock();
            return false;
        }
    }
    if n >= MAX_FROZEN {
        REGISTRY.lock.unlock();
        return false;
    }
    entries[n] = Some(Frozen { name, src });
    REGISTRY.len.store(n + 1, Ordering::Relaxed);
    REGISTRY.lock.unlock();
    true
}

/// Look up a frozen module by exact name (upstream `mp_find_frozen_module`'s
/// `MP_IMPORT_STAT_FILE` case).
pub fn find(name: &str) -> Option<Frozen> {
    REGISTRY.lock.lock();
    let entries = unsafe { &*REGISTRY.entries.get() };
    let n = REGISTRY.len.load(Ordering::Relaxed);
    let found = entries.iter().take(n).flatten().find(|e| e.name == name).copied();
    REGISTRY.lock.unlock();
    found
}

/// Source bytes for a frozen module, or `None` if it isn't registered.
pub fn load_as_source(name: &str) -> Option<&'static [u8]> {
    find(name).map(|f| f.src)
}

/// Host/test helper: drop all registrations (mirrors
/// `qstr::reset_dynamic_for_test`). Does not free anything -- every entry
/// is `'static`.
pub unsafe fn reset_for_test() {
    REGISTRY.lock.lock();
    REGISTRY.len.store(0, Ordering::Relaxed);
    REGISTRY.lock.unlock();
}
