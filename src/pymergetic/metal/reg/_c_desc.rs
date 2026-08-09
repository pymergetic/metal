//! C declare bridge — `pm_metal_reg_mod_desc_t` → kernel [`RegMod`].
//!
//! C cannot emit a bit-identical [`RegMod`] (`&str` / `Cell` layout). Floor
//! C modules declare a C descriptor + `PM_METAL_REG_MOD*`; this pool builds
//! the real Rust `RegMod` once and loads it.

use core::cell::Cell;
use core::ffi::{c_char, c_void, CStr};
use core::mem::MaybeUninit;
use core::ptr;

use crate::declare::RegImport;
use crate::entry::RegExport;
use crate::kernel::{self, HookFn, RegMod};
use crate::ledger::{LANG_C, LANG_PY, LANG_RS};
use crate::spin::Spin;

const MOD_POOL: usize = 64;
const EXPORT_POOL: usize = 512;
const IMPORT_POOL: usize = 256;

#[repr(C)]
pub struct PmMetalRegExport {
    pub name: *const c_char,
    pub ptr: *mut c_void,
}

#[repr(C)]
pub struct PmMetalRegImport {
    pub module: *const c_char,
    pub func: *const c_char,
}

#[repr(C)]
pub struct PmMetalRegModDesc {
    pub name: *const c_char,
    pub exports: *mut PmMetalRegExport,
    pub n_exports: u32,
    pub imports: *mut PmMetalRegImport,
    pub n_imports: u32,
    pub register_symbols: Option<extern "C" fn(*mut c_void) -> i32>,
    pub ctx: *mut c_void,
    pub lang: u8,
}

struct CModSlot {
    desc: *mut PmMetalRegModDesc,
    export_base: usize,
    n_exports: usize,
}

struct CPool {
    lock: Spin,
    mods: [MaybeUninit<RegMod>; MOD_POOL],
    exports: [MaybeUninit<RegExport>; EXPORT_POOL],
    imports: [MaybeUninit<RegImport>; IMPORT_POOL],
    slots: [MaybeUninit<CModSlot>; MOD_POOL],
    n_mods: usize,
    n_exports: usize,
    n_imports: usize,
    n_slots: usize,
}

static mut POOL: CPool = CPool {
    lock: Spin::new(),
    mods: [const { MaybeUninit::uninit() }; MOD_POOL],
    exports: [const { MaybeUninit::uninit() }; EXPORT_POOL],
    imports: [const { MaybeUninit::uninit() }; IMPORT_POOL],
    slots: [const { MaybeUninit::uninit() }; MOD_POOL],
    n_mods: 0,
    n_exports: 0,
    n_imports: 0,
    n_slots: 0,
};

unsafe fn cstr_static(p: *const c_char) -> Option<&'static str> {
    if p.is_null() {
        return None;
    }
    CStr::from_ptr(p).to_str().ok()
}

fn normalize_lang(lang: u8) -> u8 {
    match lang {
        x if x == LANG_RS => LANG_RS,
        x if x == LANG_PY => LANG_PY,
        _ => LANG_C,
    }
}

extern "C" fn c_register_symbols_wrap(ctx: *mut c_void) -> i32 {
    if ctx.is_null() {
        return -1;
    }
    let slot = unsafe { &*ctx.cast::<CModSlot>() };
    let desc = unsafe { &*slot.desc };
    let rc = match desc.register_symbols {
        Some(f) => f(desc.ctx),
        None => 0,
    };
    if rc != 0 {
        return rc;
    }
    /* Mirror C export.ptr → RegExport after the C register hook. */
    unsafe {
        let pool = &raw mut POOL;
        for i in 0..slot.n_exports {
            let c_ex = &*desc.exports.add(i);
            let re = (*pool).exports[slot.export_base + i].assume_init_ref();
            re.publish(c_ex.ptr);
        }
    }
    0
}

/// Load a C-declared module descriptor into the RegMod ring.
///
/// Idempotent: returns 0 if `desc.name` is already loaded. `-1` on bad
/// args, pool exhaustion, or hook failure.
///
/// # Safety
/// `desc` and all nested name/export/import pointers must remain valid
/// for the process lifetime (typical: static storage from `PM_METAL_REG_MOD*`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_mod_load_c(desc: *mut PmMetalRegModDesc) -> i32 {
    if desc.is_null() {
        return -1;
    }
    let d = &mut *desc;
    let Some(name) = cstr_static(d.name) else {
        return -1;
    };
    if kernel::find_mod(name).is_some() {
        return 0;
    }

    let n_ex = d.n_exports as usize;
    let n_im = d.n_imports as usize;
    if n_ex > 0 && d.exports.is_null() {
        return -1;
    }
    if n_im > 0 && d.imports.is_null() {
        return -1;
    }

    let pool = &raw mut POOL;
    (*pool).lock.lock();

    if (*pool).n_mods >= MOD_POOL
        || (*pool).n_slots >= MOD_POOL
        || (*pool).n_exports + n_ex > EXPORT_POOL
        || (*pool).n_imports + n_im > IMPORT_POOL
    {
        (*pool).lock.unlock();
        return -1;
    }

    let export_base = (*pool).n_exports;
    let import_base = (*pool).n_imports;
    let mod_idx = (*pool).n_mods;
    let slot_idx = (*pool).n_slots;

    for i in 0..n_ex {
        let c_ex = &*d.exports.add(i);
        let Some(ename) = cstr_static(c_ex.name) else {
            (*pool).lock.unlock();
            return -1;
        };
        (*pool).exports[export_base + i].write(RegExport::new(ename));
    }
    for i in 0..n_im {
        let c_im = &*d.imports.add(i);
        let Some(mname) = cstr_static(c_im.module) else {
            (*pool).lock.unlock();
            return -1;
        };
        let Some(fname) = cstr_static(c_im.func) else {
            (*pool).lock.unlock();
            return -1;
        };
        (*pool).imports[import_base + i].write(RegImport::new(mname, fname));
    }

    (*pool).n_exports += n_ex;
    (*pool).n_imports += n_im;

    let exports_ptr = (*pool).exports.as_ptr().add(export_base) as *const RegExport;
    let imports_ptr = (*pool).imports.as_ptr().add(import_base) as *const RegImport;
    let exports: &'static [RegExport] = core::slice::from_raw_parts(exports_ptr, n_ex);
    let imports: &'static [RegImport] = core::slice::from_raw_parts(imports_ptr, n_im);

    let slot = CModSlot {
        desc,
        export_base,
        n_exports: n_ex,
    };
    (*pool).slots[slot_idx].write(slot);
    (*pool).n_slots += 1;
    let slot_ptr = (*pool).slots[slot_idx].as_mut_ptr();

    let register: Option<HookFn> = Some(c_register_symbols_wrap);
    let m = RegMod {
        name,
        unloadable: false,
        parent: None,
        ctx: slot_ptr.cast(),
        on_load: None,
        register_symbols: register,
        connect_symbols: None,
        on_registrations_updated: None,
        deregister_symbols: None,
        on_unload: None,
        exports,
        imports,
        lang: normalize_lang(d.lang),
        raw_next: Cell::new(ptr::null()),
        raw_prev: Cell::new(ptr::null()),
    };
    (*pool).mods[mod_idx].write(m);
    (*pool).n_mods += 1;
    let m_ref: &'static RegMod = &*(*pool).mods[mod_idx].as_ptr();

    (*pool).lock.unlock();

    if kernel::load(m_ref) != 0 {
        return -1;
    }
    0
}
