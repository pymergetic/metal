//! Kernel root table: loaded modules + lifecycle (load/connect/unload).
//!
//! `pymergetic.metal` itself is the first module loaded through here
//! (see `boot`'s bootstrap), permanently `unloadable = false`. Every
//! other genuinely `unloadable` provider (wasm, Python attach) loads the
//! same way; permanently-linked modules never call `load`/`unload` at
//! all -- they use a generated fast-path face instead (see
//! `docs/definitions/module.md`).

use core::cell::UnsafeCell;
use core::ffi::c_void;

use crate::declare::ImportRow;
use crate::entry::RegEntry;
use crate::spin::Spin;

pub type HookFn = extern "C" fn(*mut c_void) -> i32;

/// One loaded module's lifecycle shape + published border. Non-generic
/// (unlike [`crate::RegModStatic`]) so the kernel table can hold
/// heterogeneous modules without a const-generic size parameter leaking
/// into the table's own type -- `entries`/`imports` borrow the module's
/// own fixed-capacity arrays as plain slices.
pub struct RegMod {
    pub name: &'static str,
    pub unloadable: bool,
    /// Name of this module's parent, if any. Unloading a module cascades
    /// to every *currently loaded* module naming it as parent first, so
    /// a package's own child module substructure never outlives it.
    pub parent: Option<&'static str>,
    pub ctx: *mut c_void,
    pub on_load: Option<HookFn>,
    pub register_symbols: Option<HookFn>,
    pub connect_symbols: Option<HookFn>,
    pub on_registrations_updated: Option<HookFn>,
    pub deregister_symbols: Option<HookFn>,
    pub on_unload: Option<HookFn>,
    pub entries: &'static [RegEntry],
    pub imports: &'static [ImportRow],
}

// Safety: `RegMod` itself is plain data once built (hooks are function
// pointers, `entries`/`imports` are `'static` slices of `Sync` types);
// `ctx` is only ever dereferenced inside the owning module's own hooks.
unsafe impl Sync for RegMod {}

pub const KERNEL_MAX: usize = 32;

struct KernelTable {
    lock: Spin,
    mods: UnsafeCell<[Option<&'static RegMod>; KERNEL_MAX]>,
}

unsafe impl Sync for KernelTable {}

impl KernelTable {
    const fn new() -> Self {
        Self {
            lock: Spin::new(),
            mods: UnsafeCell::new([None; KERNEL_MAX]),
        }
    }

    /// Point-in-time copy of the occupied-slot array. `RegMod` refs are
    /// `'static` and `Copy`, so this is cheap and lets callers walk the
    /// table (including running hooks that may themselves call back into
    /// `load`/`find_*`) without holding the lock across a hook call.
    fn snapshot(&self) -> [Option<&'static RegMod>; KERNEL_MAX] {
        self.lock.lock();
        let s = unsafe { *self.mods.get() };
        self.lock.unlock();
        s
    }

    fn insert(&self, m: &'static RegMod) -> bool {
        self.lock.lock();
        let slots = unsafe { &mut *self.mods.get() };
        let mut ok = false;
        for slot in slots.iter_mut() {
            if slot.is_none() {
                *slot = Some(m);
                ok = true;
                break;
            }
        }
        self.lock.unlock();
        ok
    }

    fn remove(&self, name: &str) {
        self.lock.lock();
        let slots = unsafe { &mut *self.mods.get() };
        for slot in slots.iter_mut() {
            if slot.map(|m| m.name == name).unwrap_or(false) {
                *slot = None;
                break;
            }
        }
        self.lock.unlock();
    }
}

static KERNEL: KernelTable = KernelTable::new();

fn run_hook(m: &'static RegMod, hook: Option<HookFn>) -> i32 {
    match hook {
        Some(f) => f(m.ctx),
        None => 0,
    }
}

pub fn find_mod(name: &str) -> Option<&'static RegMod> {
    KERNEL.snapshot().into_iter().flatten().find(|m| m.name == name)
}

pub fn find_entry(module: &str, func: &str) -> Option<&'static RegEntry> {
    find_mod(module).and_then(|m| m.entries.iter().find(|e| e.name == func))
}

fn connect_one(m: &'static RegMod) {
    for imp in m.imports {
        imp.set(find_entry(imp.module, imp.func));
    }
    let _ = run_hook(m, m.connect_symbols);
}

/// Re-resolve every loaded module's import slots against the current
/// kernel table. Called after every load/unload; also safe to call any
/// time a caller wants a fresh reconnect pass.
pub fn connect_all() {
    for m in KERNEL.snapshot().into_iter().flatten() {
        connect_one(m);
        let _ = run_hook(m, m.on_registrations_updated);
    }
}

/// `on_load` -> `register_symbols` -> global connect pass. `0` on
/// success; `-1` if the name is already loaded, the table is full, or a
/// hook reports failure (in which case the module is rolled back out of
/// the table before returning).
pub fn load(m: &'static RegMod) -> i32 {
    if find_mod(m.name).is_some() {
        return -1;
    }
    if !KERNEL.insert(m) {
        return -1;
    }
    if run_hook(m, m.on_load) != 0 || run_hook(m, m.register_symbols) != 0 {
        KERNEL.remove(m.name);
        return -1;
    }
    connect_all();
    0
}

/// Cascading unload: every currently-loaded module naming `name` as
/// parent is unloaded first (depth-first), then `name` itself --
/// `deregister_symbols` (withdrawing every entry), `on_unload`, removal
/// from the kernel table, and a final reconnect pass so every other
/// module's import slots that pointed here drop back to null.
///
/// Refuses (`-1`, no side effect) if:
/// - `m.unloadable` is `false` -- a permanently-linked module (or a
///   sticky package), most notably the kernel namespace root itself,
///   cannot be unloaded, full stop (see `docs/definitions/module.md`
///   "Kernel is a module too").
/// - any entry still has a live [`crate::acquire`]/[`crate::release`]
///   pair outstanding (`refs != 0`) on this module or on any child it
///   would cascade into -- a live caller keeps its provider (and the
///   provider's own children) alive.
pub fn unload(name: &str) -> i32 {
    let Some(m) = find_mod(name) else {
        return -1;
    };
    if !m.unloadable {
        return -1;
    }
    for child in KERNEL.snapshot().into_iter().flatten() {
        if child.parent == Some(name) {
            if unload(child.name) != 0 {
                return -1;
            }
        }
    }
    if m.entries.iter().any(|e| e.refs() != 0) {
        return -1;
    }
    let _ = run_hook(m, m.deregister_symbols);
    for e in m.entries {
        e.withdraw();
    }
    let _ = run_hook(m, m.on_unload);
    KERNEL.remove(name);
    connect_all();
    0
}

pub fn count() -> usize {
    KERNEL.snapshot().into_iter().flatten().count()
}
