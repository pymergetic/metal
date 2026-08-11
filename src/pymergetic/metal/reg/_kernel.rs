//! Kernel root table: loaded modules + lifecycle (load/connect/unload).
//!
//! **Transitional façade:** module identity / soft-import resolve SoT is
//! µPy `sys.modules` via `pm_mod_*`. This circular ring is bookkeeping for
//! floor load hooks + ledger — do not grow new module-OS features here.
//! Prefer `pm_mod_publish` / `pm_mod_connect_import` for new work.
//!
//! `pymergetic.metal` itself is the first module loaded through here
//! (see `boot`'s bootstrap), permanently `unloadable = false`. Every
//! other genuinely `unloadable` provider (wasm, Python attach) loads the
//! same way; permanently-linked modules never call `load`/`unload` at
//! all -- they use a generated fast-path face instead (see
//! `docs/definitions/module.md`).
//!
//! Storage is decentralized: each loaded [`RegMod`] carries its own raw-
//! tape link (`raw_next`/`raw_prev`, a circular doubly-linked ring), not
//! a fixed-size central array -- so the number of concurrently loaded
//! modules has no compile-time cap. [`KernelTable::snapshot`] still walks
//! that ring into a bounded stack buffer (`SNAPSHOT_MAX`, generous, not a
//! storage limit) so callers can run hooks without holding the lock
//! across a call that might itself re-enter `load`/`find_mod` -- same
//! safety property the original fixed-array version had, just backed by
//! a real ring instead of a preallocated slot table.
//!
//! The sorted tape (`sorted_head`, global `(module, func)` ordering for
//! O(log n) bisection) from the design is deliberately not built yet:
//! nothing today needs anything but "find by exact name" (a handful of
//! dozens of modules, linear-scan-cheap), and building it before a real
//! caller needs sorted enumeration would be exactly the kind of
//! speculative machinery this tree's own rules warn against.

use core::cell::Cell;
use core::ffi::{c_char, c_void};

use crate::declare::RegImport;
use crate::entry::RegExport;
use crate::ledger::{self, HONESTY_OK, HONESTY_STUB, LANG_RS, ROLE_MUSCLE, VIA_IMPORT_ROW};
use crate::spin::Spin;

/* Quiesce muscle: pymergetic_metal_async::quiesce (C ABI). */
extern "C" {
    fn pm_metal_async_quiesce_request();
    fn pm_metal_async_quiesce_all_parked() -> i32;
    fn pm_metal_async_quiesce_release();
    /* µPy SoT — may be absent on tiny stubs; weak-style via optional link. */
    fn pm_mod_publish(
        name: *const c_char,
        container: i32,
        exports: *const c_void,
        n_exports: u32,
    ) -> i32;
    fn pm_mod_export_set(module: *const c_char, func: *const c_char, fn_ptr: *mut c_void) -> i32;
    fn pm_mod_resolve_native(module: *const c_char, func: *const c_char) -> *mut c_void;
}

/// Mirrors C `pm_mod_container_t` (`extmod/wasmmod/include/pm_mod.h`) —
/// the code-container tag `pm_mod_publish` records, not a module kind.
/// Keep the discriminants in lockstep with the C enum; this is an FFI
/// mirror, not an independent Rust-side definition.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[allow(dead_code)] // full C enum mirror; only Resident is published from Rust today
pub enum PmModContainer {
    Resident = 0,
    Wasm = 1,
    Aot = 2,
    Elf = 3,
}

pub type HookFn = extern "C" fn(*mut c_void) -> i32;

/// One loaded module's lifecycle shape + published border. Non-generic
/// (unlike [`crate::RegModStatic`]) so the kernel table can hold
/// heterogeneous modules without a const-generic size parameter leaking
/// into the table's own type -- `exports`/`imports` borrow the module's
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
    pub exports: &'static [RegExport],
    pub imports: &'static [RegImport],
    /// Ledger language for published exports (`LANG_C` / `LANG_RS` / `LANG_PY`).
    pub lang: u8,
    /// Raw-tape link (insertion-order circular ring) -- mutated only
    /// under [`KernelTable`]'s lock by `insert`/`remove`. Null (self,
    /// really -- see `insert`) until this node is actually spliced in.
    pub raw_next: Cell<*const RegMod>,
    pub raw_prev: Cell<*const RegMod>,
}

impl RegMod {
    /// Permanent floor module backed by a [`crate::RegModStatic`]'s slices.
    pub const fn from_static(
        name: &'static str,
        exports: &'static [RegExport],
        imports: &'static [RegImport],
        register_symbols: Option<HookFn>,
    ) -> Self {
        Self {
            name,
            unloadable: false,
            parent: None,
            ctx: core::ptr::null_mut(),
            on_load: None,
            register_symbols,
            connect_symbols: None,
            on_registrations_updated: None,
            deregister_symbols: None,
            on_unload: None,
            exports,
            imports,
            lang: LANG_RS,
            raw_next: Cell::new(core::ptr::null()),
            raw_prev: Cell::new(core::ptr::null()),
        }
    }

    /// Inventory-only module (frozen Py package, no C/RS exports yet).
    pub const fn py_inventory(name: &'static str) -> Self {
        Self::from_static(name, &[], &[], None)
    }
}

// Safety: `RegMod`'s non-`Cell` fields are plain data once built (hooks
// are function pointers, `exports`/`imports` are `'static` slices of
// `Sync` types); `ctx` is only ever dereferenced inside the owning
// module's own hooks. `raw_next`/`raw_prev` are `Cell`s (not `Sync` on
// their own) but are only ever touched while holding the owning
// `KernelTable`'s lock, same external-synchronization contract the
// original fixed-array table already relied on.
unsafe impl Sync for RegMod {}

/// Generous cap on one non-locked walk of the raw ring, **not** a
/// storage limit -- the ring itself can hold any number of modules;
/// this only bounds how many a single [`KernelTable::snapshot`] call
/// can report before a caller would need to re-snapshot.
pub const SNAPSHOT_MAX: usize = 256;

struct KernelTable {
    lock: Spin,
    /// Any node currently on the ring, or null if empty. Raw order is
    /// just insertion order -- where you start doesn't matter.
    ring: Cell<*const RegMod>,
}

unsafe impl Sync for KernelTable {}

impl KernelTable {
    const fn new() -> Self {
        Self {
            lock: Spin::new(),
            ring: Cell::new(core::ptr::null()),
        }
    }

    /// Point-in-time copy of every loaded module into a caller-owned
    /// stack buffer. `RegMod` refs are `'static` and `Copy`, so this is
    /// cheap and lets callers walk the table (including running hooks
    /// that may themselves call back into `load`/`find_*`) without
    /// holding the lock across a hook call.
    fn snapshot(&self) -> [Option<&'static RegMod>; SNAPSHOT_MAX] {
        self.lock.lock();
        let mut buf: [Option<&'static RegMod>; SNAPSHOT_MAX] = [None; SNAPSHOT_MAX];
        let start = self.ring.get();
        if !start.is_null() {
            let mut cur = start;
            let mut n = 0usize;
            loop {
                if n >= SNAPSHOT_MAX {
                    break;
                }
                // Safety: every node reachable from `ring` was inserted
                // via `insert` and only ever removed (never freed) via
                // `remove`, both under this same lock.
                let node = unsafe { &*cur };
                buf[n] = Some(node);
                n += 1;
                cur = node.raw_next.get();
                if cur == start {
                    break;
                }
            }
        }
        self.lock.unlock();
        buf
    }

    fn insert(&self, m: &'static RegMod) -> bool {
        self.lock.lock();
        let head = self.ring.get();
        let m_ptr = m as *const RegMod;
        if head.is_null() {
            m.raw_next.set(m_ptr);
            m.raw_prev.set(m_ptr);
            self.ring.set(m_ptr);
        } else {
            // Safety: `head` is either null (handled above) or a live
            // node inserted the same way.
            let tail = unsafe { &*head }.raw_prev.get();
            unsafe { &*tail }.raw_next.set(m_ptr);
            m.raw_prev.set(tail);
            m.raw_next.set(head);
            unsafe { &*head }.raw_prev.set(m_ptr);
        }
        self.lock.unlock();
        true
    }

    fn remove(&self, name: &str) {
        self.lock.lock();
        let start = self.ring.get();
        if !start.is_null() {
            let mut cur = start;
            loop {
                // Safety: see `snapshot`.
                let node = unsafe { &*cur };
                if node.name == name {
                    let next = node.raw_next.get();
                    let prev = node.raw_prev.get();
                    if next == cur {
                        self.ring.set(core::ptr::null());
                    } else {
                        unsafe { &*prev }.raw_next.set(next);
                        unsafe { &*next }.raw_prev.set(prev);
                        if self.ring.get() == cur {
                            self.ring.set(next);
                        }
                    }
                    node.raw_next.set(core::ptr::null());
                    node.raw_prev.set(core::ptr::null());
                    break;
                }
                cur = node.raw_next.get();
                if cur == start {
                    break;
                }
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

pub fn find_export(module: &str, func: &str) -> Option<&'static RegExport> {
    find_mod(module).and_then(|m| m.exports.iter().find(|e| e.name == func))
}

#[inline]
pub fn find_entry(module: &str, func: &str) -> Option<&'static RegExport> {
    find_export(module, func)
}

fn cstr_tmp(s: &str, buf: &mut [u8]) -> *const c_char {
    let n = core::cmp::min(s.len(), buf.len().saturating_sub(1));
    buf[..n].copy_from_slice(&s.as_bytes()[..n]);
    buf[n] = 0;
    buf.as_ptr() as *const c_char
}

/// Mirror RegMod exports into __pm_modules.
fn mirror_to_upy(m: &'static RegMod) {
    let mut name_buf = [0u8; 128];
    let name_c = cstr_tmp(m.name, &mut name_buf);
    let _ = unsafe { pm_mod_publish(name_c, PmModContainer::Resident as i32, core::ptr::null(), 0) };
    for e in m.exports {
        let mut fn_buf = [0u8; 64];
        let fn_c = cstr_tmp(e.name, &mut fn_buf);
        let _ = unsafe { pm_mod_export_set(name_c, fn_c, e.get() as *mut c_void) };
    }
}

fn resolve_native_upy(module: &str, func: &str) -> *mut c_void {
    let mut mbuf = [0u8; 128];
    let mut fbuf = [0u8; 64];
    let mc = cstr_tmp(module, &mut mbuf);
    let fc = cstr_tmp(func, &mut fbuf);
    unsafe { pm_mod_resolve_native(mc, fc) }
}

/// CString pointers (already NUL-terminated) → µPy native resolve.
pub(crate) fn resolve_native_c(module: *const u8, func: *const u8) -> *mut c_void {
    if module.is_null() || func.is_null() {
        return core::ptr::null_mut();
    }
    unsafe { pm_mod_resolve_native(module as *const c_char, func as *const c_char) }
}

/// Lazy connect for path-included RegImports (see [`crate::resolve_import`]).
pub(crate) fn resolve_import_row(row: &RegImport) {
    let fnp = resolve_native_upy(row.module, row.func);
    if !fnp.is_null() {
        row.set_fn(fnp);
        return;
    }
    if let Some(e) = find_export(row.module, row.func) {
        row.set(Some(e));
    }
}

fn connect_one(m: &'static RegMod) {
    for imp in m.imports {
        /* SoT: sys.modules native table first; RegMod ring is transitional. */
        let mut fnp = resolve_native_upy(imp.module, imp.func);
        let export = find_export(imp.module, imp.func);
        if fnp.is_null() {
            if let Some(e) = export {
                fnp = e.get() as *mut c_void;
            }
        }
        imp.set(export);
        imp.set_fn(fnp);
        /* Cold ledger caller edge — inspect only; hot path still one atomic load. */
        let honesty = if !fnp.is_null() {
            HONESTY_OK
        } else {
            HONESTY_STUB
        };
        let _ = ledger::LEDGER.add_caller(
            imp.module.as_bytes(),
            imp.func.as_bytes(),
            LANG_RS,
            m.name.as_bytes(),
            VIA_IMPORT_ROW,
            honesty,
        );
    }
    let _ = run_hook(m, m.connect_symbols);
}

fn publish_entries_to_ledger(m: &'static RegMod) {
    for e in m.exports {
        let ptr = e.get();
        let honesty = if ptr.is_null() {
            HONESTY_STUB
        } else {
            HONESTY_OK
        };
        let _ = ledger::LEDGER.add_callee(
            m.name.as_bytes(),
            e.name.as_bytes(),
            m.lang,
            ROLE_MUSCLE,
            honesty,
            false,
            false,
            b"",
            b"regmod_entry",
            ptr,
        );
    }
}

/// Re-resolve every loaded module's import slots against µPy SoT (+ ring façade).
/// Called after every load/unload; also safe to call any time a caller wants
/// a fresh reconnect pass. Caches into import slots — no per-call lookup.
pub fn connect_all() {
    for m in KERNEL.snapshot().into_iter().flatten() {
        connect_one(m);
        let _ = run_hook(m, m.on_registrations_updated);
    }
}

// Notify C seat table when a RegMod loads (pack / dyn faces).
extern "C" {
    fn pm_metal_reg_seat_on_mod_load(full_module: *const u8);
}

/// `on_load` -> `register_symbols` -> global connect pass. `0` on
/// success; `-1` if the name is already loaded or a hook reports
/// failure (in which case the module is rolled back out of the table
/// before returning).
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
    mirror_to_upy(m);
    publish_entries_to_ledger(m);
    connect_all();
    /* Unloadable packs share the C seat table (Inspect/REPL intel).
     * Permanently-linked floor modules already have glue seats — do not
     * invent import-test seats for names with no µPy face. */
    if m.unloadable {
        let mut name_buf = [0u8; 128];
        let n = m.name.len().min(name_buf.len() - 1);
        name_buf[..n].copy_from_slice(&m.name.as_bytes()[..n]);
        unsafe {
            pm_metal_reg_seat_on_mod_load(name_buf.as_ptr());
        }
    }
    0
}

/// Wait until every runner has parked, run `f` with the registry
/// guaranteed to have zero concurrent callers anywhere in it, then
/// resume every runner. If async was never started (`n_runners == 0`,
/// e.g. a host smoke test with no engine running), `all_parked` is
/// vacuously true immediately -- no wait, no behavior change from
/// before quiesce existed.
///
/// On UP (one thread draining every runner via `run_poll_all`), the wait
/// must *drive* polls -- a bare spin never reaches the quiesce checkpoint
/// in `poll_runner`, so park flags stay false forever. Each poll lets a
/// runner observe the request, call `park_runner`, and return.
fn with_quiesce<R>(f: impl FnOnce() -> R) -> R {
    unsafe {
        extern "C" {
            fn pm_metal_async_run_poll_all() -> i32;
            fn pm_metal_async_n_runners() -> u32;
        }
        pm_metal_async_quiesce_request();
        let mut spins = 0u32;
        while pm_metal_async_quiesce_all_parked() == 0 {
            if pm_metal_async_n_runners() > 0 {
                let _ = pm_metal_async_run_poll_all();
            }
            core::hint::spin_loop();
            spins = spins.wrapping_add(1);
            if spins > 50_000_000 {
                /* Wedge guard -- still run f so unload can fail closed
                 * rather than hang the firmware forever. */
                break;
            }
        }
        let r = f();
        pm_metal_async_quiesce_release();
        r
    }
}

/// Cascading unload: every currently-loaded module naming `name` as
/// parent is unloaded first (depth-first), then `name` itself --
/// `deregister_symbols` (withdrawing every entry), `on_unload`, removal
/// from the kernel table, and a final reconnect pass so every other
/// module's import slots that pointed here drop back to null.
///
/// Refuses (`-1`, no side effect) if `m.unloadable` is `false` -- a
/// permanently-linked module (or a sticky package), most notably the
/// kernel namespace root itself, cannot be unloaded, full stop (see
/// `docs/definitions/module.md` "Kernel is a module too").
///
/// The mutation itself (`deregister_symbols` through the reconnect
/// pass) runs inside a global quiesce: every async runner is parked at
/// its own next dispatch checkpoint first, so there is no concurrent
/// caller anywhere in the system that could be mid-call through an
/// entry this unload is about to withdraw. That replaces the old
/// per-entry refcount interlock (`acquire`/`release`) entirely -- a
/// provider's entries no longer need a live-caller count to be safe to
/// withdraw, because nothing can be calling at all during the withdraw.
pub fn unload(name: &str) -> i32 {
    let Some(m) = find_mod(name) else {
        return -1;
    };
    if !m.unloadable {
        return -1;
    }
    for child in KERNEL.snapshot().into_iter().flatten() {
        if child.parent == Some(name) && unload(child.name) != 0 {
            return -1;
        }
    }
    with_quiesce(|| {
        let _ = run_hook(m, m.deregister_symbols);
        for e in m.exports {
            e.withdraw();
        }
        let _ = run_hook(m, m.on_unload);
        KERNEL.remove(name);
        connect_all();
    });
    0
}

pub fn count() -> usize {
    KERNEL.snapshot().into_iter().flatten().count()
}

/// Loaded module at insertion-order `index`, or None if OOB.
pub fn mod_at(index: usize) -> Option<&'static RegMod> {
    KERNEL.snapshot().into_iter().flatten().nth(index)
}
