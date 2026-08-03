//! bindcatalog — typed Metal / extmod bind specs for FunNative attach.
//!
//! Reg publishes bare function pointers; this table supplies the calling
//! convention (`objfun_native` kind + arity) so import never falls back to
//! string markers or a wrong `fn() -> i32` guess. Symbols not listed here
//! are attached only when they match the short floor-probe set (`ready` /
//! `listen` / `open`) as [`KIND_I32_0`].

use core::ffi::c_void;

use crate::upy::extmod::{
    modbinascii, modheapq, modjson, modos, modplatform, modrandom, modre, modtime, modvfs,
};
use crate::upy::py::nativeglue;
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{objfun_native, objint, objnone, objstr};
use crate::upy::py::objects::objfun_native::{
    KIND_I32_0, KIND_I32_1, KIND_OBJ, ObjFn,
};

/// How to wrap a published symbol (or a fixed Metal/extmod body).
#[derive(Clone, Copy)]
pub enum BindKind {
    /// Use the reg pointer with this native kind / arity.
    Reg { kind: u8, n_args: u8 },
    /// Ignore reg ptr; call this `KIND_OBJ` wrapper instead.
    Obj { f: ObjFn, n_args: u8 },
    /// Present on reg but not safe/meaningful as a Python callable yet.
    Omit,
}

pub fn lookup(module: &str, name: &str) -> Option<BindKind> {
    match (module, name) {
        // --- pymergetic.metal.py (C ABI edges) ---
        ("pymergetic.metal.py", "ready")
        | ("pymergetic.metal.py", "gc_enabled")
        | ("pymergetic.metal.py", "gc_collect")
        | ("pymergetic.metal.py", "loop_step")
        | ("pymergetic.metal.py", "loop_reset")
        | ("pymergetic.metal.py", "loop_last_result_i32")
        | ("pymergetic.metal.py", "loop_last_result_valid")
        | ("pymergetic.metal.py", "shell_start")
        | ("pymergetic.metal.py", "shell_running")
        | ("pymergetic.metal.py", "proof_print")
        | ("pymergetic.metal.py", "proof_await")
        | ("pymergetic.metal.py", "proof_concurrency") => {
            Some(BindKind::Reg {
                kind: KIND_I32_0,
                n_args: 0,
            })
        }
        ("pymergetic.metal.py", "sleep_us") => Some(BindKind::Obj {
            f: py_sleep_us,
            n_args: 1,
        }),
        ("pymergetic.metal.py", "alloc")
        | ("pymergetic.metal.py", "free")
        | ("pymergetic.metal.py", "await")
        | ("pymergetic.metal.py", "loop_feed") => Some(BindKind::Omit),

        // --- microdot (Rust C ABI) ---
        ("pymergetic.metal.net.http.microdot", "register") => Some(BindKind::Obj {
            f: md_register,
            n_args: 0,
        }),
        ("pymergetic.metal.net.http.microdot", "handle") => Some(BindKind::Reg {
            kind: KIND_I32_1,
            n_args: 1,
        }),
        ("pymergetic.metal.net.http.microdot", "get") => Some(BindKind::Obj {
            f: md_get_refuse,
            n_args: 1,
        }),
        ("pymergetic.metal.net.http.microdot", "route") => Some(BindKind::Obj {
            f: md_route_refuse,
            n_args: 2,
        }),
        ("pymergetic.metal.net.http.microdot", "bind_reg") => Some(BindKind::Obj {
            f: md_bind_reg,
            n_args: 0,
        }),

        _ => None,
    }
}

/// Floor probe names that are actually `fn() -> i32` on published modules.
pub fn floor_probe_i32_0(name: &str) -> bool {
    matches!(name, "ready" | "listen" | "open")
}

/// Extra attrs to attach after walking reg (not always published as rows).
pub fn extras(module: &str) -> &'static [(&'static str, ObjFn, u8)] {
    match module {
        "pymergetic.metal.net.http.microdot" => &[("bind_reg", md_bind_reg, 0)],
        _ => &[],
    }
}

/// Store a FunNative for `name` on `mod_obj` per catalog / floor rules.
pub unsafe fn store_bound(
    mod_obj: MpObj,
    module: &str,
    name: &str,
    reg_ptr: *const c_void,
) {
    if name.is_empty() || name == "__module__" {
        return;
    }
    let (ptr, kind, n_args) = match lookup(module, name) {
        Some(BindKind::Omit) => return,
        Some(BindKind::Obj { f, n_args }) => (f as *const c_void, KIND_OBJ, n_args),
        Some(BindKind::Reg { kind, n_args }) => {
            if reg_ptr.is_null() {
                return;
            }
            (reg_ptr, kind, n_args)
        }
        None => {
            if !floor_probe_i32_0(name) || reg_ptr.is_null() {
                return;
            }
            (reg_ptr, KIND_I32_0, 0)
        }
    };
    let key = obj::new_qstr(crate::upy::py::qstr::from_str(name));
    let fun = objfun_native::new(ptr, n_args, kind);
    if fun != obj::OBJ_NULL {
        let _ = crate::upy::py::objects::objmodule::store_attr(mod_obj, key, fun);
    }
}

pub unsafe fn store_obj_fn(mod_obj: MpObj, name: &str, f: ObjFn, n_args: u8) {
    let key = obj::new_qstr(crate::upy::py::qstr::from_str(name));
    let fun = objfun_native::new(f as *const c_void, n_args, KIND_OBJ);
    if fun != obj::OBJ_NULL {
        let _ = crate::upy::py::objects::objmodule::store_attr(mod_obj, key, fun);
    }
}

// -- py module OBJ wrappers -------------------------------------------------

unsafe fn py_sleep_us(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    let us = nativeglue::as_i32(args[0])? as u64;
    extern "C" {
        fn pm_metal_py_sleep_us(us: u64) -> u32;
    }
    let h = pm_metal_py_sleep_us(us);
    Some(nativeglue::from_i32(h as i32))
}

// -- microdot OBJ wrappers --------------------------------------------------

extern "C" {
    fn pm_metal_net_http_microdot_register() -> u32;
    fn pm_metal_net_http_microdot_bind_reg() -> i32;
}

unsafe fn md_register(args: &[MpObj]) -> Option<MpObj> {
    if !args.is_empty() {
        return None;
    }
    Some(nativeglue::from_i32(pm_metal_net_http_microdot_register() as i32))
}

unsafe fn md_bind_reg(args: &[MpObj]) -> Option<MpObj> {
    if !args.is_empty() {
        return None;
    }
    Some(nativeglue::from_i32(pm_metal_net_http_microdot_bind_reg()))
}

/// `get(path, handler)` needs a C function pointer; Python cannot supply one.
unsafe fn md_get_refuse(args: &[MpObj]) -> Option<MpObj> {
    let _ = args;
    None
}

/// `route(method, path, handler)` needs a C function pointer.
unsafe fn md_route_refuse(args: &[MpObj]) -> Option<MpObj> {
    let _ = args;
    None
}

// -- extmod OBJ wrappers ----------------------------------------------------

pub unsafe fn json_dumps(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    modjson::dumps(args[0])
}

pub unsafe fn json_loads(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    modjson::loads_obj(args[0])
}

pub unsafe fn binascii_hexlify(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    modbinascii::hexlify_obj(args[0])
}

pub unsafe fn binascii_unhexlify(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    modbinascii::unhexlify_obj(args[0])
}

pub unsafe fn heapq_heappush(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 2 {
        return None;
    }
    if modheapq::heappush(args[0], args[1]) {
        Some(objnone::get())
    } else {
        None
    }
}

pub unsafe fn heapq_heappop(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    modheapq::heappop(args[0])
}

pub unsafe fn random_getrandbits(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    let n = nativeglue::as_i32(args[0])?;
    if n < 0 {
        return None;
    }
    modrandom::getrandbits(n as u32)
}

pub unsafe fn time_ticks_us(args: &[MpObj]) -> Option<MpObj> {
    if !args.is_empty() {
        return None;
    }
    Some(objint::from_isize(modtime::ticks_us() as isize))
}

pub unsafe fn time_ticks_ms(args: &[MpObj]) -> Option<MpObj> {
    if !args.is_empty() {
        return None;
    }
    Some(objint::from_isize(modtime::ticks_ms() as isize))
}

pub unsafe fn platform_platform(args: &[MpObj]) -> Option<MpObj> {
    if !args.is_empty() {
        return None;
    }
    Some(modplatform::platform())
}

pub unsafe fn os_uname(args: &[MpObj]) -> Option<MpObj> {
    if !args.is_empty() {
        return None;
    }
    Some(modos::uname())
}

pub unsafe fn os_listdir(args: &[MpObj]) -> Option<MpObj> {
    if args.is_empty() {
        return modos::listdir(b".");
    }
    if args.len() != 1 {
        return None;
    }
    modos::listdir_obj(args[0])
}

pub unsafe fn re_compile(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    modre::compile_obj(args[0])
}

pub unsafe fn re_match(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 2 {
        return None;
    }
    // `re.match(pattern, string)` one-shot, or compiled pattern + string.
    if objstr::as_bytes(args[0]).is_some() && objstr::as_bytes(args[1]).is_some() {
        let pat = objstr::as_bytes(args[0])?;
        let text = objstr::as_bytes(args[1])?;
        return Some(crate::upy::py::objects::objbool::get(modre::match_str(
            pat, text,
        )));
    }
    modre::match_obj(args[0], args[1])
}

pub unsafe fn vfs_open(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 && args.len() != 2 {
        return None;
    }
    let flags = if args.len() == 2 {
        let f = nativeglue::as_i32(args[1])?;
        if f < 0 {
            return None;
        }
        f as u32
    } else {
        modvfs::O_RDONLY
    };
    modvfs::open_obj(args[0], flags)
}

pub unsafe fn vfs_listdir(args: &[MpObj]) -> Option<MpObj> {
    if args.is_empty() {
        return crate::upy::extmod::vfs::listdir(b".");
    }
    if args.len() != 1 {
        return None;
    }
    let path = objstr::as_bytes(args[0])?;
    crate::upy::extmod::vfs::listdir(path)
}

pub unsafe fn asyncio_sleep_ms(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    let ms = nativeglue::as_i32(args[0])?;
    if ms < 0 {
        return None;
    }
    let t = crate::upy::extmod::asyncio::sleep_ms(ms as u64)?;
    Some(nativeglue::from_i32(t.handle as i32))
}

pub unsafe fn asyncio_run(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    let h = nativeglue::as_i32(args[0])?;
    if h < 0 {
        return None;
    }
    if crate::upy::extmod::asyncio::run_until(h as u32) {
        Some(objnone::get())
    } else {
        None
    }
}
