//! objfun_native — native (Rust/C ABI) callable object: wraps a plain
//! function pointer as a real, addressable `MpObj` -- the honest
//! replacement for the old `builtinimport.rs` string `"reg"` marker (see
//! that module's doc history): `pymergetic.metal.py.ready` now resolves
//! to one of these, and `vm.rs`'s `CALL_FUNCTION` makes a real ABI call
//! through it, not a marker check.
//!
//! Calling conventions (`kind`), matching the shapes this VM's
//! `CALL_FUNCTION` and Metal bind / extmod layer need:
//! - [`KIND_I32_0`] / [`KIND_I32_RET_0ARG`]: `extern "C" fn() -> i32`
//! - [`KIND_I32_1`]: `extern "C" fn(i32) -> i32`
//! - [`KIND_I32_2`]: `extern "C" fn(i32, i32) -> i32`
//! - [`KIND_STR_I32`]: `extern "C" fn(*const u8) -> i32` (NUL C string
//!   from an `objstr` arg)
//! - [`KIND_PRINT`]: `modbuiltins::print_obj` (Rust builtin)
//! - [`KIND_OBJ`]: `unsafe fn(args: &[MpObj]) -> Option<MpObj>` stored
//!   as `*const c_void` -- extmod wrappers that speak `MpObj` directly

use core::ffi::c_void;

use crate::upy::py::builtin::modbuiltins;
use crate::upy::py::malloc;
use crate::upy::py::nativeglue;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{objnone, TypeDesc, TYPE_FUN_NATIVE};

pub const KIND_I32_0: u8 = 0;
/// Alias for [`KIND_I32_0`] (older call sites / bind tables).
pub const KIND_I32_RET_0ARG: u8 = KIND_I32_0;
pub const KIND_I32_1: u8 = 1;
pub const KIND_I32_2: u8 = 2;
pub const KIND_STR_I32: u8 = 3;
pub const KIND_PRINT: u8 = 4;
pub const KIND_OBJ: u8 = 5;

/// Extmod / bind wrapper: take already-popped positional args, return
/// a result object or `None` on failure (VM turns that into `Exception`).
pub type ObjFn = unsafe fn(args: &[MpObj]) -> Option<MpObj>;

#[repr(C)]
pub struct FunNative {
    pub base: MpObjBase,
    pub ptr: *const c_void,
    pub n_args: u8,
    pub kind: u8,
}

/// `ptr` is unused (may be null) for `kind == KIND_PRINT`, which always
/// calls the fixed `modbuiltins::print_obj` Rust function instead.
pub unsafe fn new(ptr: *const c_void, n_args: u8, kind: u8) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<FunNative>()) as *mut FunNative;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_FUN_NATIVE as *const TypeDesc) as *const u8);
    (*p).ptr = ptr;
    (*p).n_args = n_args;
    (*p).kind = kind;
    p as MpObj
}

unsafe fn as_ref(o: MpObj) -> Option<*const FunNative> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const FunNative;
    if (*p).base.type_ptr != (&TYPE_FUN_NATIVE as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn is_fun_native(o: MpObj) -> bool {
    as_ref(o).is_some()
}

/// Call the native function with `args` already popped off the VM stack
/// in left-to-right order. `None` on an arg-count mismatch, an unknown
/// `kind`, a null `ptr` for a pointer-calling kind, a marshalling
/// failure, or the callee itself reporting failure -- the caller
/// (`vm.rs`) turns that into the usual `Exception` sentinel.
pub unsafe fn call(o: MpObj, args: &[MpObj]) -> Option<MpObj> {
    let p = as_ref(o)?;
    // `KIND_OBJ` validates arity inside the wrapper (e.g. `range` 1..3).
    if (*p).kind != KIND_OBJ && args.len() != (*p).n_args as usize {
        return None;
    }
    match (*p).kind {
        KIND_PRINT => {
            if !modbuiltins::print_obj(args[0]) {
                return None;
            }
            Some(objnone::get())
        }
        KIND_I32_0 => {
            if (*p).ptr.is_null() {
                return None;
            }
            let f: extern "C" fn() -> i32 = core::mem::transmute((*p).ptr);
            Some(nativeglue::from_i32(f()))
        }
        KIND_I32_1 => {
            if (*p).ptr.is_null() {
                return None;
            }
            let a0 = nativeglue::arg0_as_i32(args)?;
            let f: extern "C" fn(i32) -> i32 = core::mem::transmute((*p).ptr);
            Some(nativeglue::from_i32(f(a0)))
        }
        KIND_I32_2 => {
            if (*p).ptr.is_null() {
                return None;
            }
            let (a0, a1) = nativeglue::args_as_i32_2(args)?;
            let f: extern "C" fn(i32, i32) -> i32 = core::mem::transmute((*p).ptr);
            Some(nativeglue::from_i32(f(a0, a1)))
        }
        KIND_STR_I32 => {
            if (*p).ptr.is_null() || args.len() != 1 {
                return None;
            }
            let cstr = nativeglue::as_cstr_ptr(args[0])?;
            let f: extern "C" fn(*const u8) -> i32 = core::mem::transmute((*p).ptr);
            Some(nativeglue::from_i32(f(cstr)))
        }
        KIND_OBJ => {
            if (*p).ptr.is_null() {
                return None;
            }
            let f: ObjFn = core::mem::transmute((*p).ptr);
            f(args)
        }
        _ => None,
    }
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_ref(o) {
        malloc::m_free(p as *mut u8);
    }
}
