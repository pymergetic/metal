//! Flat registry table — full module name + func -> fn pointer, linear scan.
//! Fixed slots (no heap); spin-guarded for SMP.
//!
//! This is the *dynamic/late* registration layer only. Compile-time-known
//! floor modules that are never unloaded do not publish through here at
//! all -- they use a generated fast-path `extern "C"` face instead (see
//! `docs/definitions/module.md`). Genuinely `unloadable` providers (wasm,
//! Python) publish through the static per-module lifecycle instead
//! (`_entry.rs` / `_declare.rs` / `_kernel.rs`), which keys each module's
//! own small fixed entry array by name rather than sharing one fat slot
//! array across unrelated modules. `wasm`'s own module registration
//! already moved to that static tier (`wasm/__init__.rs`'s
//! `pm_metal_wasm_register` builds a `RegMod` and calls
//! `pm_metal_reg_mod_load`, same as any compile-time module); this table
//! remains only for callers that genuinely do not know their (module,
//! func) pairs until runtime and have no per-load `RegMod` shape of
//! their own — Python attach (see `pm_metal_reg_register`'s still-real
//! callers in `py/`, `net/ssh`, `net/http/*`) — so `SLOT_MAX` only needs
//! to cover that late-registration tier, not the entire floor's symbol
//! count; shrunk from 512 accordingly.

use core::cell::UnsafeCell;
use core::ffi::c_void;

use crate::spin::Spin;

pub const MODULE_MAX: usize = 128;
pub const FUNC_MAX: usize = 64;
pub const SLOT_MAX: usize = 64;

#[derive(Clone, Copy)]
struct Slot {
    used: bool,
    module_len: u8,
    func_len: u8,
    module: [u8; MODULE_MAX],
    func: [u8; FUNC_MAX],
    ptr: *const c_void,
}

impl Slot {
    const fn empty() -> Self {
        Self {
            used: false,
            module_len: 0,
            func_len: 0,
            module: [0; MODULE_MAX],
            func: [0; FUNC_MAX],
            ptr: core::ptr::null(),
        }
    }
}

pub struct Table {
    lock: Spin,
    slots: UnsafeCell<[Slot; SLOT_MAX]>,
    count: UnsafeCell<usize>,
}

// Safety: all slot mutations run under `lock`.
unsafe impl Sync for Table {}

impl Table {
    pub const fn new() -> Self {
        Self {
            lock: Spin::new(),
            slots: UnsafeCell::new([Slot::empty(); SLOT_MAX]),
            count: UnsafeCell::new(0),
        }
    }

    pub fn register(&self, module: &[u8], func: &[u8], ptr: *const c_void) -> i32 {
        if module.is_empty() || func.is_empty() || ptr.is_null() {
            return -1;
        }
        if module.len() >= MODULE_MAX || func.len() >= FUNC_MAX {
            return -1;
        }
        self.lock.lock();
        let rc = unsafe { self.register_locked(module, func, ptr) };
        self.lock.unlock();
        rc
    }

    unsafe fn register_locked(&self, module: &[u8], func: &[u8], ptr: *const c_void) -> i32 {
        let slots = &mut *self.slots.get();
        if let Some(i) = self.find_locked(slots, module, func) {
            slots[i].ptr = ptr;
            return 0;
        }
        for i in 0..SLOT_MAX {
            if !slots[i].used {
                let s = &mut slots[i];
                s.used = true;
                s.module_len = module.len() as u8;
                s.func_len = func.len() as u8;
                s.module[..module.len()].copy_from_slice(module);
                s.func[..func.len()].copy_from_slice(func);
                s.ptr = ptr;
                *self.count.get() += 1;
                return 0;
            }
        }
        -1
    }

    pub fn lookup(&self, module: &[u8], func: &[u8]) -> *const c_void {
        self.lock.lock();
        let slots = unsafe { &*self.slots.get() };
        let p = self
            .find_locked(slots, module, func)
            .map(|i| slots[i].ptr)
            .unwrap_or(core::ptr::null());
        self.lock.unlock();
        p
    }

    fn find_locked(&self, slots: &[Slot; SLOT_MAX], module: &[u8], func: &[u8]) -> Option<usize> {
        for i in 0..SLOT_MAX {
            let s = &slots[i];
            if !s.used {
                continue;
            }
            let m = &s.module[..s.module_len as usize];
            let f = &s.func[..s.func_len as usize];
            if m == module && f == func {
                return Some(i);
            }
        }
        None
    }

    pub fn count(&self) -> usize {
        self.lock.lock();
        let n = unsafe { *self.count.get() };
        self.lock.unlock();
        n
    }

    /// Used-slot at `index` (0..count), under lock. Returns false if OOB.
    pub fn at(
        &self,
        index: usize,
        module_out: &mut [u8],
        func_out: &mut [u8],
        ptr_out: &mut *const c_void,
    ) -> bool {
        self.lock.lock();
        let slots = unsafe { &*self.slots.get() };
        let mut n = 0usize;
        let mut ok = false;
        for i in 0..SLOT_MAX {
            let s = &slots[i];
            if !s.used {
                continue;
            }
            if n == index {
                let ml = s.module_len as usize;
                let fl = s.func_len as usize;
                if ml < module_out.len() && fl < func_out.len() {
                    module_out[..ml].copy_from_slice(&s.module[..ml]);
                    module_out[ml] = 0;
                    func_out[..fl].copy_from_slice(&s.func[..fl]);
                    func_out[fl] = 0;
                    *ptr_out = s.ptr;
                    ok = true;
                }
                break;
            }
            n += 1;
        }
        self.lock.unlock();
        ok
    }
}

pub fn cstr_bytes<'a>(p: *const u8, max: usize) -> Option<&'a [u8]> {
    if p.is_null() {
        return None;
    }
    let mut n = 0usize;
    unsafe {
        while *p.add(n) != 0 {
            n += 1;
            if n > max {
                return None;
            }
        }
        Some(core::slice::from_raw_parts(p, n))
    }
}
