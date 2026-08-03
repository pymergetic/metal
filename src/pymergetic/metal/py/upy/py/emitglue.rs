//! emitglue — finish a compiled emitter buffer into a runnable object
//! (upstream `py/emitglue.c` mirror slice: `mp_obj_new_code`/
//! `mp_make_function_from_raw_code`, minus the persistent `.mpy`
//! load/link path this Metal mirror doesn't have).

use crate::upy::py::emitcommon::MAX_CONST_OBJS;
use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::objfun;

/// Finished bytecode + the locals-slot count `vm::CodeState` needs for it
/// + a small const-object table for `LOAD_CONST_OBJ` (FunBc values for
/// `def`, etc.). Trimmed relative to upstream `mp_raw_code_t` (no child
/// raw-code list, no native/viper variant).
pub struct RawCode {
    code: *mut u8,
    len: usize,
    pub n_state: usize,
    consts: [MpObj; MAX_CONST_OBJS],
    n_consts: usize,
}

impl RawCode {
    /// # Safety
    /// `code` must be a Metal-heap allocation of at least `len` bytes,
    /// owned by the caller (this takes ownership -- freed on `Drop`).
    /// `consts` words are copied by value; this does **not** take
    /// ownership of heap objects they point at (a `FunBc` embedded for
    /// `def` is expected to live on in globals after `STORE_NAME`).
    pub unsafe fn from_raw(code: *mut u8, len: usize, n_state: usize, consts: &[MpObj]) -> Self {
        let mut table = [obj::OBJ_NULL; MAX_CONST_OBJS];
        let n = core::cmp::min(consts.len(), MAX_CONST_OBJS);
        table[..n].copy_from_slice(&consts[..n]);
        Self {
            code,
            len,
            n_state,
            consts: table,
            n_consts: n,
        }
    }

    pub fn as_bytecode(&self) -> &[u8] {
        if self.code.is_null() {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(self.code, self.len) }
        }
    }

    pub fn as_consts(&self) -> &[MpObj] {
        &self.consts[..self.n_consts]
    }
}

impl Drop for RawCode {
    fn drop(&mut self) {
        unsafe { malloc::m_free(self.code) };
    }
}

/// Wrap a compiled `RawCode` as a callable bytecode-function object
/// (upstream `mp_make_function_from_raw_code`). `objfun::new` copies the
/// bytes and const table into its own allocation, so `raw` still
/// owns/frees its bytecode buffer independently afterwards.
pub unsafe fn make_function(raw: &RawCode) -> MpObj {
    objfun::new(raw.as_bytecode(), raw.n_state, raw.as_consts())
}
