//! builtinimport — resolve modules via sys.modules, builtins, or Metal `reg`.

use core::ffi::c_void;

use crate::upy::extmod::{
    modbinascii, modheapq, modjson, modos, modplatform, modrandom, modre, modtime, modvfs,
};
use crate::upy::py::builtin::{modbuiltins, moderrno, modsys};
use crate::upy::py::obj::{self, MpObj};
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
    for probe in REG_PROBES {
        let p = unsafe { pm_metal_reg_bind(mod_c.as_ptr(), probe.as_ptr()) };
        if !p.is_null() {
            return true;
        }
    }
    false
}

/// Attach `reg`-bound callables we can discover onto the module as raw ptr objs.
/// B3 stores the pointer as a small-int of the address truncated — host smoke
/// checks presence via `reg_attr_bound` instead. Here we store a str marker.
unsafe fn attach_reg_markers(mod_obj: MpObj, full_name: &str) {
    let mod_c = cstr(full_name);
    for probe in REG_PROBES {
        let p = pm_metal_reg_bind(mod_c.as_ptr(), probe.as_ptr());
        if p.is_null() {
            continue;
        }
        let name = core::str::from_utf8(&probe[..probe.len() - 1]).unwrap_or("sym");
        let key = obj::new_qstr(qstr::from_str(name));
        let marker = objstr::new(b"reg");
        let _ = objmodule::store_attr(mod_obj, key, marker);
    }
}

unsafe fn store_marker(m: MpObj, name: &str, val: &[u8]) {
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstr::from_str(name)),
        objstr::new(val),
    );
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

unsafe fn import_extmod(name: &str) -> Option<MpObj> {
    let m = match name {
        "json" | "ujson" => {
            let m = make_named(name)?;
            store_marker(m, "dumps", b"fn");
            store_marker(m, "loads", b"fn");
            let _ = modjson::dumps;
            m
        }
        "binascii" | "ubinascii" => {
            let m = make_named(name)?;
            store_marker(m, "hexlify", b"fn");
            store_marker(m, "unhexlify", b"fn");
            let _ = modbinascii::hexlify;
            m
        }
        "heapq" | "uheapq" => {
            let m = make_named(name)?;
            store_marker(m, "heappush", b"fn");
            store_marker(m, "heappop", b"fn");
            let _ = modheapq::heappush;
            m
        }
        "random" | "urandom" => {
            let m = make_named(name)?;
            store_marker(m, "getrandbits", b"fn");
            let _ = modrandom::getrandbits;
            m
        }
        "time" | "utime" => {
            let m = make_named(name)?;
            store_marker(m, "ticks_us", b"fn");
            store_marker(m, "ticks_ms", b"fn");
            let _ = modtime::ticks_us;
            m
        }
        "platform" => {
            let m = make_named(name)?;
            store_marker(m, "platform", b"fn");
            let _ = modplatform::platform;
            m
        }
        "os" | "uos" => {
            let m = make_named(name)?;
            store_marker(m, "uname", b"fn");
            store_marker(m, "listdir", b"fn");
            let _ = modos::uname;
            m
        }
        "re" | "ure" => {
            let m = make_named(name)?;
            store_marker(m, "compile", b"fn");
            store_marker(m, "match", b"fn");
            let _ = modre::compile;
            m
        }
        "vfs" => {
            let m = make_named(name)?;
            store_marker(m, "open", b"fn");
            store_marker(m, "listdir", b"fn");
            let _ = modvfs::open;
            m
        }
        "asyncio" | "uasyncio" => {
            let m = make_named(name)?;
            store_marker(m, "sleep_ms", b"fn");
            store_marker(m, "run", b"fn");
            store_marker(m, "Event", b"type");
            store_marker(m, "Lock", b"type");
            let _ = crate::upy::extmod::asyncio::sleep_ms;
            m
        }
        _ => return None,
    };
    let _ = modsys::modules_set_str(name, m);
    Some(m)
}

/// Import by name. Builtins: `sys`, `errno`, `builtins`. Extmod keep-list (B5).
/// Metal: any full module name that has at least one symbol on `reg`.
pub unsafe fn import_module(name: &str) -> Option<MpObj> {
    modsys::init();
    if let Some(m) = modsys::modules_get_str(name) {
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
            } else if !reg_module_present(name) {
                return None;
            } else {
                let m = objmodule::new(qstr::from_str(name));
                if m == obj::OBJ_NULL {
                    return None;
                }
                let _ = objmodule::store_attr(
                    m,
                    obj::new_qstr(qstrdefs::QSTR_NAME),
                    objstr::new(name.as_bytes()),
                );
                attach_reg_markers(m, name);
                let _ = modsys::modules_set_str(name, m);
                m
            }
        }
    };
    Some(m)
}

/// True if `attr` on an imported Metal module was filled from `reg`.
pub unsafe fn has_reg_marker(mod_obj: MpObj, attr: &str) -> bool {
    let key = obj::new_qstr(qstr::from_str(attr));
    match objmodule::load_attr(mod_obj, key) {
        Some(v) => matches!(objstr::as_bytes(v), Some(b"reg")),
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
