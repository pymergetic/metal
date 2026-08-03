//! builtinimport — resolve modules via sys.modules, builtins, or Metal `reg`.
//!
//! Metal `reg` modules become real Python module objects with typed native
//! callables ([`objfun_native`] + [`bindcatalog`]), not string markers.
//! Dotted imports (`import a.b.c`) create every parent package and wire
//! `parent.child` attributes so `a.b.c.ready()` attribute chains work; the
//! value returned to `IMPORT_NAME` is the top-level package (`a`), matching
//! CPython. Extmod keep-list modules expose FunNative wrappers over the
//! real Rust bodies in `upy/extmod/`.

use core::ffi::c_void;

use crate::upy::py::bindcatalog;
use crate::upy::py::builtin::{modbuiltins, moderrno, modsys};
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::objfun_native;
use crate::upy::py::objects::objmodule;
use crate::upy::py::objects::objstr;
use crate::upy::py::qstr;
use crate::upy::py::qstrdefs;

extern "C" {
    fn pm_metal_reg_bind(full_module: *const u8, func: *const u8) -> *const c_void;
    fn pm_metal_reg_register(
        full_module: *const u8,
        func: *const u8,
        ptr: *const c_void,
    ) -> i32;
    fn pm_metal_reg_mod_entry_count(full_module: *const u8) -> u32;
    fn pm_metal_reg_mod_entry_at(
        full_module: *const u8,
        index: u32,
        name_out: *mut u8,
        name_cap: u32,
        out_ptr: *mut *const c_void,
    ) -> i32;
    fn pm_metal_reg_count() -> u32;
    fn pm_metal_reg_dyn_at(
        index: u32,
        module_out: *mut u8,
        module_cap: u32,
        func_out: *mut u8,
        func_cap: u32,
        out_ptr: *mut *const c_void,
    ) -> i32;
}

/// Probe funcs that indicate a Metal module is published on `reg`.
const REG_PROBES: &[&[u8]] = &[b"ready\0", b"__module__\0", b"listen\0", b"open\0"];

fn cstr(s: &str) -> alloc_buf {
    alloc_buf::from_str(s)
}

struct alloc_buf {
    buf: [u8; 160],
    len: usize,
}

impl alloc_buf {
    fn from_str(s: &str) -> Self {
        let mut buf = [0u8; 160];
        let n = core::cmp::min(s.len(), buf.len() - 1);
        buf[..n].copy_from_slice(&s.as_bytes()[..n]);
        buf[n] = 0;
        Self { buf, len: n }
    }
    fn as_ptr(&self) -> *const u8 {
        self.buf.as_ptr()
    }
}

fn reg_module_present(full_name: &str) -> bool {
    let mod_c = cstr(full_name);
    if unsafe { pm_metal_reg_mod_entry_count(mod_c.as_ptr()) } > 0 {
        return true;
    }
    for probe in REG_PROBES {
        let p = unsafe { pm_metal_reg_bind(mod_c.as_ptr(), probe.as_ptr()) };
        if !p.is_null() {
            return true;
        }
    }
    let n = unsafe { pm_metal_reg_count() };
    let mut mod_buf = [0u8; 160];
    let mut fn_buf = [0u8; 64];
    for i in 0..n {
        let mut ptr: *const c_void = core::ptr::null();
        if unsafe {
            pm_metal_reg_dyn_at(
                i,
                mod_buf.as_mut_ptr(),
                mod_buf.len() as u32,
                fn_buf.as_mut_ptr(),
                fn_buf.len() as u32,
                &mut ptr,
            )
        } != 0
        {
            continue;
        }
        let mlen = mod_buf.iter().position(|&b| b == 0).unwrap_or(mod_buf.len());
        if mlen == full_name.len() && &mod_buf[..mlen] == full_name.as_bytes() {
            return true;
        }
    }
    false
}

/// Attach every published `reg` entry for `full_name` as a typed native.
unsafe fn attach_reg_callables(mod_obj: MpObj, full_name: &str) {
    let mod_c = cstr(full_name);
    let n = pm_metal_reg_mod_entry_count(mod_c.as_ptr());
    if n > 0 {
        let mut name_buf = [0u8; 64];
        for i in 0..n {
            let mut ptr: *const c_void = core::ptr::null();
            if pm_metal_reg_mod_entry_at(
                mod_c.as_ptr(),
                i,
                name_buf.as_mut_ptr(),
                name_buf.len() as u32,
                &mut ptr,
            ) != 0
            {
                continue;
            }
            let nlen = name_buf.iter().position(|&b| b == 0).unwrap_or(0);
            if nlen == 0 {
                continue;
            }
            if let Ok(name) = core::str::from_utf8(&name_buf[..nlen]) {
                bindcatalog::store_bound(mod_obj, full_name, name, ptr);
            }
        }
    } else {
        let dyn_n = pm_metal_reg_count();
        let mut mod_buf = [0u8; 160];
        let mut fn_buf = [0u8; 64];
        let mut any = false;
        for i in 0..dyn_n {
            let mut ptr: *const c_void = core::ptr::null();
            if pm_metal_reg_dyn_at(
                i,
                mod_buf.as_mut_ptr(),
                mod_buf.len() as u32,
                fn_buf.as_mut_ptr(),
                fn_buf.len() as u32,
                &mut ptr,
            ) != 0
            {
                continue;
            }
            let mlen = mod_buf.iter().position(|&b| b == 0).unwrap_or(0);
            if mlen != full_name.len() || &mod_buf[..mlen] != full_name.as_bytes() {
                continue;
            }
            let nlen = fn_buf.iter().position(|&b| b == 0).unwrap_or(0);
            if nlen == 0 {
                continue;
            }
            if let Ok(name) = core::str::from_utf8(&fn_buf[..nlen]) {
                bindcatalog::store_bound(mod_obj, full_name, name, ptr);
                any = true;
            }
        }
        if !any {
            for probe in REG_PROBES {
                let p = pm_metal_reg_bind(mod_c.as_ptr(), probe.as_ptr());
                if p.is_null() {
                    continue;
                }
                let name = core::str::from_utf8(&probe[..probe.len() - 1]).unwrap_or("sym");
                bindcatalog::store_bound(mod_obj, full_name, name, p);
            }
        }
    }
    for &(name, f, n_args) in bindcatalog::extras(full_name) {
        bindcatalog::store_obj_fn(mod_obj, name, f, n_args);
    }
}

unsafe fn make_named(name: &str) -> Option<MpObj> {
    let m = objmodule::new(qstr::from_str(name));
    if m == obj::OBJ_NULL {
        return None;
    }
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_NAME),
        objstr::new(name.as_bytes()),
    );
    Some(m)
}

unsafe fn get_or_create_pkg(path: &str) -> Option<MpObj> {
    if let Some(m) = modsys::modules_get_str(path) {
        return Some(m);
    }
    let m = make_named(path)?;
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstr::from_str("__path__")),
        objstr::new(b""),
    );
    let _ = modsys::modules_set_str(path, m);
    Some(m)
}

/// Import a dotted Metal reg module; return the top-level package object.
unsafe fn import_reg_dotted(full: &str) -> Option<MpObj> {
    if !reg_module_present(full) {
        return None;
    }
    let mut path_buf = [0u8; 160];
    let mut path_len = 0usize;
    let mut parent = obj::OBJ_NULL;
    let mut top = obj::OBJ_NULL;
    let bytes = full.as_bytes();
    let mut seg_start = 0usize;
    for i in 0..=bytes.len() {
        let end = i == bytes.len();
        if !end && bytes[i] != b'.' {
            continue;
        }
        let seg = &bytes[seg_start..i];
        if seg.is_empty() {
            return None;
        }
        if path_len > 0 {
            if path_len + 1 + seg.len() >= path_buf.len() {
                return None;
            }
            path_buf[path_len] = b'.';
            path_len += 1;
        } else if seg.len() >= path_buf.len() {
            return None;
        }
        path_buf[path_len..path_len + seg.len()].copy_from_slice(seg);
        path_len += seg.len();
        let path = core::str::from_utf8(&path_buf[..path_len]).ok()?;
        let is_leaf = end;
        let m = if is_leaf {
            if let Some(existing) = modsys::modules_get_str(path) {
                existing
            } else {
                let m = make_named(path)?;
                attach_reg_callables(m, path);
                let _ = modsys::modules_set_str(path, m);
                m
            }
        } else {
            get_or_create_pkg(path)?
        };
        if parent != obj::OBJ_NULL {
            if let Ok(seg_s) = core::str::from_utf8(seg) {
                let key = obj::new_qstr(qstr::from_str(seg_s));
                let _ = objmodule::store_attr(parent, key, m);
            }
        } else {
            top = m;
        }
        parent = m;
        seg_start = i + 1;
    }
    if top == obj::OBJ_NULL {
        None
    } else {
        Some(top)
    }
}

unsafe fn import_extmod(name: &str) -> Option<MpObj> {
    let m = make_named(name)?;
    match name {
        "json" | "ujson" => {
            bindcatalog::store_obj_fn(m, "dumps", bindcatalog::json_dumps, 1);
            bindcatalog::store_obj_fn(m, "loads", bindcatalog::json_loads, 1);
        }
        "binascii" | "ubinascii" => {
            bindcatalog::store_obj_fn(m, "hexlify", bindcatalog::binascii_hexlify, 1);
            bindcatalog::store_obj_fn(m, "unhexlify", bindcatalog::binascii_unhexlify, 1);
        }
        "heapq" | "uheapq" => {
            bindcatalog::store_obj_fn(m, "heappush", bindcatalog::heapq_heappush, 2);
            bindcatalog::store_obj_fn(m, "heappop", bindcatalog::heapq_heappop, 1);
        }
        "random" | "urandom" => {
            bindcatalog::store_obj_fn(m, "getrandbits", bindcatalog::random_getrandbits, 1);
        }
        "time" | "utime" => {
            bindcatalog::store_obj_fn(m, "ticks_us", bindcatalog::time_ticks_us, 0);
            bindcatalog::store_obj_fn(m, "ticks_ms", bindcatalog::time_ticks_ms, 0);
        }
        "platform" => {
            bindcatalog::store_obj_fn(m, "platform", bindcatalog::platform_platform, 0);
        }
        "os" | "uos" => {
            bindcatalog::store_obj_fn(m, "uname", bindcatalog::os_uname, 0);
            bindcatalog::store_obj_fn(m, "listdir", bindcatalog::os_listdir, 1);
        }
        "re" | "ure" => {
            bindcatalog::store_obj_fn(m, "compile", bindcatalog::re_compile, 1);
            bindcatalog::store_obj_fn(m, "match", bindcatalog::re_match, 2);
        }
        "vfs" => {
            bindcatalog::store_obj_fn(m, "open", bindcatalog::vfs_open, 1);
            bindcatalog::store_obj_fn(m, "listdir", bindcatalog::vfs_listdir, 1);
        }
        "asyncio" | "uasyncio" => {
            // Event/Lock are Rust types without a finished Python ctor -- omit.
            bindcatalog::store_obj_fn(m, "sleep_ms", bindcatalog::asyncio_sleep_ms, 1);
            bindcatalog::store_obj_fn(m, "run", bindcatalog::asyncio_run, 1);
        }
        _ => return None,
    }
    let _ = modsys::modules_set_str(name, m);
    Some(m)
}

/// Import by name. Builtins: `sys`, `errno`, `builtins`. Extmod keep-list.
/// Metal: any full module name that has at least one symbol on `reg`.
/// Dotted Metal names return the **top-level package** (CPython `import a.b.c`).
pub unsafe fn import_module(name: &str) -> Option<MpObj> {
    modsys::init();
    if let Some(m) = modsys::modules_get_str(name) {
        if name.contains('.') {
            if let Some(top) = name.split('.').next() {
                if let Some(t) = modsys::modules_get_str(top) {
                    return Some(t);
                }
            }
        }
        return Some(m);
    }

    let m = match name {
        "sys" => modsys::module(),
        "errno" => {
            let m = moderrno::make_module();
            if m == obj::OBJ_NULL {
                return None;
            }
            let _ = modsys::modules_set_str("errno", m);
            m
        }
        "builtins" => {
            let m = modbuiltins::make_module();
            if m == obj::OBJ_NULL {
                return None;
            }
            let _ = modsys::modules_set_str("builtins", m);
            m
        }
        _ => {
            if let Some(m) = import_extmod(name) {
                m
            } else if name.contains('.') {
                return import_reg_dotted(name);
            } else if !reg_module_present(name) {
                return None;
            } else {
                let m = make_named(name)?;
                attach_reg_callables(m, name);
                let _ = modsys::modules_set_str(name, m);
                m
            }
        }
    };
    Some(m)
}

/// True if `attr` on an imported Metal module is a native callable.
pub unsafe fn has_reg_marker(mod_obj: MpObj, attr: &str) -> bool {
    let key = obj::new_qstr(qstr::from_str(attr));
    match objmodule::load_attr(mod_obj, key) {
        Some(v) => objfun_native::is_fun_native(v),
        None => false,
    }
}

/// Publish a `__module__` sentinel on `reg` for `full_name` (optional helper).
pub unsafe fn publish_module_sentinel(full_name: &str) -> i32 {
    static SENTINEL: u8 = 1;
    let mod_c = cstr(full_name);
    pm_metal_reg_register(
        mod_c.as_ptr(),
        b"__module__\0".as_ptr(),
        &SENTINEL as *const u8 as *const c_void,
    )
}
