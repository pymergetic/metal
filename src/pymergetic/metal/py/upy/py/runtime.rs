//! runtime — bring-up + VM return kinds (full runtime grows in later bands).

use super::mpstate;
use super::obj::MpObj;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum VmReturnKind {
    Normal = 0,
    Yield = 1,
    Exception = 2,
}

/// Initialize sole mpstate + qstr (idempotent).
pub fn init() {
    mpstate::init();
}

/// Sentinel used when the minimal VM hits an unknown opcode.
pub fn unsupported_opcode_obj() -> MpObj {
    // Distinct from NULL/STOP; REPR_A immediate-ish sentinel word.
    super::obj::OBJ_SENTINEL
}
